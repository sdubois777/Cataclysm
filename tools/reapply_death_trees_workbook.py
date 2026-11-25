"""Re-apply the Death class trees' rows to the design workbook.

    python tools/reapply_death_trees_workbook.py            # write the rows, then regenerate the CSVs
    python tools/reapply_death_trees_workbook.py --check    # say what would change, write nothing
    python tools/reapply_death_trees_workbook.py --workbook <copy.xlsx> --no-generate

WHY THIS EXISTS. The three Death class trees (the Soul Collector, the Necromancer
and the Shadow) were approved on 2026-11-24 and entered `docs/` as tree files. A
node's sentence lives in its tree file; the numbers behind it are rows in the
`Passive Effects` sheet of `docs/All_Things_Cataclysm.xlsx`, and each class's stat
line is rows in its `Class Stats` sheet. `tools/generate_datatables.py` turns both
into `game/Data/PassiveEffects.csv` and `game/Data/ClassStats.csv`.

The workbook is a binary git cannot merge, and other branches edit the same two
sheets (the Demonic tree rework, the War trees). So this branch's edit to it is
recorded here as data: whichever workbook `development` holds when this lands,
this script redoes the edit on it, and running it twice changes nothing the
second time.

WHAT IT WRITES. Only rows for Death nodes, keyed `SoulCollector_`, `Necromancer_`
and `Shadow_`, and only stat lines for the three Death classes. Each node's rows
(or one capstone option's) and each class's stat are found by name, never by row
number:

    no rows yet                  the rows are added at the end of the sheet
    exactly the rows written     left alone
    anything else                refused, writing nothing, because somebody has
                                 changed them since and a person has to look

TO CHANGE A ROW, edit `NODES` or `CLASS_STATS`, take `development`'s workbook and
run this once. The committed workbook must be exactly that one run: rows already
written are refused rather than rewritten, and
`tools/tests/test_reapply_death_trees_workbook.py` compares the committed sheets
with a fresh application, row order included.

IT REFUSES a workbook holding formulas, charts or images, which an openpyxl save
can drop without saying so, and after saving it proves that both sheets hold
exactly the intended rows and that no other sheet changed.

AFTERWARDS the DataTable assets need the editor step,
`python tools/run_editor_python.py tools/generate_datatable_assets.py`.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import pathlib
import subprocess
import sys

import openpyxl

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
GENERATOR = REPO_ROOT / "tools" / "generate_datatables.py"

EFFECTS_SHEET = "Passive Effects"
CLASS_SHEET = "Class Stats"

#: The Passive Effects columns this script reads and writes, by header name, that
#: the sheet must already have.
EFFECT_COLUMNS = ("Node", "Stat", "Value Kind", "Value Per Point", "Required Tags",
                  "Condition", "Condition Value", "Scale", "Scale Step", "Option",
                  "Node Name", "Reach Metres", "Min Points")

#: The columns a row here MAY state, each with the `Row` field it fills. The
#: Demonic rework's `Scale Max Steps`, then the passive row port's thirteen action
#: columns and `Per Point From` (2026-11-24). THE PORT'S FOURTEEN ARE ADDED TO THE
#: SHEET when it lacks them, after its last column: the port left them optional in
#: the generator "until the first slice that writes such a row adds them with its
#: re-apply script", and the Death trees' slice C1 (2026-11-25) is that slice.
#: `tools/generate_datatables.py` names the same thirteen in `PASSIVE_ACTION_COLUMNS`.
OPTIONAL_EFFECT_COLUMNS = (
    ("Scale Max Steps", "cap"),
    ("Action", "action"), ("Action Event", "event"), ("Fraction Of", "fraction_of"),
    ("Stack Seconds", "stack_seconds"), ("Scale Offset", "scale_offset"),
    ("Every Seconds", "every_seconds"), ("Every Nth", "every_nth"),
    ("Condition 2", "cond2"), ("Condition Value 2", "cond_value2"),
    ("Trigger Cooldown", "trigger_cooldown"), ("Event Value", "event_value"),
    ("Ailment", "ailment"), ("Damage Share", "damage_share"),
    ("Per Point From", "per_point_from"),
    # AND THE DEATH TREES' SLICE C6b3'S CHANCE (2026-11-25), ruled for Tear Loose.
    ("Chance Percent", "chance_percent"))

#: Of those, the ones that hold words rather than numbers.
TEXT_FIELDS = frozenset({"action", "event", "fraction_of", "cond2", "ailment"})

CLASS_COLUMNS = ("Class", "Stat", "Base", "Per Level")

#: The row prefixes of the three Death trees, as `tree_row_prefix` spells them.
DEATH_PREFIXES = ("SoulCollector_", "Necromancer_", "Shadow_")

#: The three Death classes, as `UCataclysmCharacterCreation::ClassesByDamageType`
#: spells them.
DEATH_CLASSES = ("Soul Collector", "Necromancer", "Shadow")


class ReapplyError(Exception):
    """Something about the workbook that makes writing the rows unsafe or impossible."""


@dataclasses.dataclass(frozen=True)
class Row:
    """One Passive Effects row, less the node it belongs to and its human label.
    An action row has no stat and no value kind: write it with `A`."""
    stat: str | None
    kind: str | None
    value: float
    tags: str | None = None
    cond: str | None = None
    cond_value: float | None = None
    scale: str | None = None
    step: float | None = None
    reach: float | None = None
    min_points: int | None = None
    cap: int | None = None
    action: str | None = None
    event: str | None = None
    fraction_of: str | None = None
    stack_seconds: float | None = None
    scale_offset: float | None = None
    every_seconds: float | None = None
    every_nth: int | None = None
    cond2: str | None = None
    cond_value2: float | None = None
    trigger_cooldown: float | None = None
    event_value: float | None = None
    ailment: str | None = None
    damage_share: float | None = None
    per_point_from: int | None = None
    chance_percent: float | None = None

    def key(self) -> tuple:
        def num(v):
            return None if v is None else float(v)
        optional = tuple((getattr(self, field) or None) if field in TEXT_FIELDS
                         else num(getattr(self, field))
                         for _, field in OPTIONAL_EFFECT_COLUMNS)
        return (self.stat or None, self.kind or None, float(self.value), self.tags or None,
                self.cond or None, num(self.cond_value), self.scale or None,
                num(self.step), num(self.reach), num(self.min_points)) + optional


R = Row


def A(action: str, value: float, **more) -> Row:
    """An action row: no stat and no value kind, the pool or status in `action`
    and the event in `event`, as the passive row port writes one."""
    return Row(None, None, value, action=action, **more)


@dataclasses.dataclass(frozen=True)
class Node:
    """One node's rows (option 0), or one capstone option's.

    `before` holds each earlier state of the rows this script once wrote, as
    tuples of rows: a node found holding exactly one of them is rewritten, and
    anything else is still refused. The pattern of
    `tools/reapply_demonic_tree_rework_workbook.py`'s `Change.before`, first
    needed in slice C4d1 (2026-11-25) for the Shadow's starting node.
    """
    key: str
    label: str
    rows: tuple
    option: int = 0
    before: tuple = ()


def damage(kind: str, value: float, **more) -> tuple:
    """"damage" in a node's sentence is both halves of the pipeline, as every
    authored tree writes it: one row on attack damage and one on spell damage."""
    return (R("attack_damage", kind, value, **more), R("spell_damage", kind, value, **more))


SC = "SoulCollector_"
NE = "Necromancer_"
SH = "Shadow_"

#: Fervour held, as the rows that scale with it name it.
HELD = "class_resource_held"

#: The Death classes' kill generator, slice B (2026-11-24): Fervour a kill pays,
#: the decay after the last kill, and that decay's grace.
PER_KILL = "fervour_per_kill"
DECAY = "fervour_kill_decay_per_second"
GRACE = "fervour_kill_decay_grace_seconds"

#: The two rarity conditions, by rung: 0 Common, 1 Elite, 2 Legendary, 3 Herald,
#: 4 Boss, 5 Cataclysm Boss.
RUNG_IS = "target_rarity_is"
RUNG_AT_LEAST = "target_rarity_at_least"

#: The scales slice C1 writes: a row's own stacks, its hits in a row on one
#: enemy (each granted by the event on the row), and the minions held.
OWN = "own_stacks"
HITS = "consecutive_hits"
MINIONS = "minions_held"

#: The scale slice C2 writes: the enemies in the character's Siphon.
SIPHONED = "enemies_in_siphon"

#: The Death rows that are data today: a stat, a condition and a scale the game
#: already has. The survey of 2026-11-24 (plan section 4.1, group (a)) found 46
#: such entries, and all 46 are here. Untouched and Through and Through say "for
#: every 2 points" and are one row per step, each granted whole from its
#: `Min Points`, the Demonic tree rework's shape.
NODES: tuple[Node, ...] = (
    # ---------------------------------------------------------------- Soul Collector
    # THE STARTING NODE, WHOLE (slice B): "1 for each enemy you kill, 3 for an
    # Elite, 5 for a Legendary, 8 for a Herald and 15 for a Boss", one row a rung
    # with a Cataclysm Boss paid as a Boss (a judgement); "1% more damage for
    # every 2 Fervour you hold"; "decays at 5 per second once 4 seconds have
    # passed since your last kill", "for 8 seconds rather than 4" after an Elite
    # or stronger kill.
    Node(SC + "basic_spine_000", "Fervour",
         damage("more", 1, scale=HELD, step=2)
         + (R(PER_KILL, "flat", 1, cond=RUNG_IS, cond_value=0),
            R(PER_KILL, "flat", 3, cond=RUNG_IS, cond_value=1),
            R(PER_KILL, "flat", 5, cond=RUNG_IS, cond_value=2),
            R(PER_KILL, "flat", 8, cond=RUNG_IS, cond_value=3),
            R(PER_KILL, "flat", 15, cond=RUNG_AT_LEAST, cond_value=4),
            R(DECAY, "flat", 5),
            R(GRACE, "flat", 4, cond=RUNG_IS, cond_value=0),
            R(GRACE, "flat", 8, cond=RUNG_AT_LEAST, cond_value=1))),
    Node(SC + "basic_spine_001", "Tithe of the Dead", (R(PER_KILL, "increased", 2),)),
    Node(SC + "basic_spine_003", "Weight of Souls", damage("increased", 1, scale=HELD, step=50)),
    # GRAVE PATIENCE, "1 second later after a kill for every 5 points".
    Node(SC + "basic_spine_004", "Grave Patience",
         tuple(R(GRACE, "flat", 1, min_points=points) for points in (5, 10))),
    Node(SC + "basic_spine_005", "Between the Dead",
         (R("movement_speed", "increased", 1, cond="seconds_after_kill", cond_value=4),)),
    Node(SC + "basic_spine_006", "Pale Ward",
         (R("damage_reduction", "increased", 1, scale=HELD, step=100),)),
    Node(SC + "basic_spine_008", "Deep Urn", (R("class_resource", "increased", 2),)),
    Node(SC + "basic_spine_010", "Held Close",
         (R("crowd_control_resistance", "increased", 3,
            cond="class_resource_points_at_least", cond_value=50),)),
    Node(SC + "basic_a_stem0", "Ossuary", (R("class_resource", "increased", 2),)),
    Node(SC + "basic_a_a0", "Cold Precision",
         (R("crit_multiplier", "increased", 1, scale=HELD, step=50),)),
    Node(SC + "basic_a_a1", "Brimming",
         (R("attack_speed", "increased", 1,
            cond="class_resource_points_at_least", cond_value=50),)),
    Node(SC + "basic_a_a2", "Full Measure",
         damage("increased", 2, cond="class_resource_at_maximum")),
    Node(SC + "basic_a_b2", "Cold Storage", (R(DECAY, "increased", -5),)),
    Node(SC + "keystone_a_kB", "Hoarder", (R(DECAY, "more", -50),)),
    Node(SC + "basic_b_b0", "Quarry", damage("increased", 2, cond=RUNG_AT_LEAST, cond_value=1)),
    Node(SC + "keystone_b_kB", "Hungry for the Strong",
         (R(PER_KILL, "more", 100, cond=RUNG_AT_LEAST, cond_value=1),)),
    Node(SC + "basic_a_b0", "Hoarded Life",
         (R("health_regen", "increased", 1, scale=HELD, step=20),)),
    # "ENEMIES WITHIN 6 METRES DEAL 1% LESS DAMAGE" IS DAMAGE TO YOU from an enemy
    # that near, the shape of the enchantment "Nearby enemies deal 10%-30% less
    # damage to you" (`damage_taken`, `more`, `opponent_within_metres`). Damage
    # those enemies deal to your minions is not reached; a judgement.
    Node(SC + "keystone_c_kB", "Deathly Presence",
         (R("damage_taken", "more", -1, cond="opponent_within_metres", cond_value=6,
            scale=HELD, step=10),)),
    Node(SC + "capstone_25", "Swift Reaper",
         (R("movement_speed", "increased", 2, scale=HELD, step=10),), option=3),
    Node(SC + "capstone_50", "Death's Focus",
         (R("crit_chance", "flat", 1, scale=HELD, step=20),), option=3),
    Node(SC + "capstone_100", "Soul Haste",
         (R("attack_speed", "increased", 3, scale=HELD, step=10),), option=3),

    # ---------------------------------------------------------------- Necromancer
    # THE STARTING NODE'S FIRST HALF. "1 each time an enemy dies within 10 metres
    # of you" is `fervour_on_enemy_death_nearby`, which
    # `UCataclysmFervour::GainOnEnemyDeathNearby` pays within its fixed 10 metres.
    # The second half, "or to one of your minions" beyond that, is new code and
    # is not built here.
    Node(NE + "basic_spine_000", "Fervour",
         (R("fervour_on_enemy_death_nearby", "flat", 1),)),
    Node(NE + "basic_spine_001", "Wider Grave", (R("class_resource", "increased", 2),)),
    Node(NE + "basic_spine_002", "Death Knell",
         (R("fervour_on_enemy_death_nearby", "increased", 3),)),
    Node(NE + "basic_spine_003", "Restless Host",
         (R("minion_damage", "increased", 1), R("minion_health", "increased", 1))),
    # COOLDOWN REDUCTION IS A FLAT SUM, as every authored source writes it: a
    # cooldown is its base divided by one plus the sum, and the enchantment
    # "Summon skills have 30%-60% reduced cooldown" is `flat` on `Keyword.Summon`.
    Node(NE + "basic_spine_006", "Raised in Haste",
         (R("cooldown_reduction", "flat", 3, tags="Keyword.Summon"),)),
    Node(NE + "basic_spine_007", "Grave Sense",
         (R("class_resource_generation", "increased", 2),)),
    Node(NE + "basic_spine_009", "Deeper Grave", (R("class_resource", "increased", 2),)),
    Node(NE + "basic_ch_c0", "Toll of the Dead",
         (R("fervour_on_enemy_death_nearby", "increased", 3),)),

    # ---------------------------------------------------------------- Shadow
    # THE STARTING NODE'S KILL HALF (slice B): "10 for each enemy you kill, 25
    # for each Elite or stronger enemy you kill", "drains at 10 per second once 4
    # seconds have passed since your last kill". Its "2 for each critical strike
    # you land" is `fervour_per_critical_strike`, built in slice C4d1
    # (2026-11-25) as ruled; the War trees' Berserker row is not written here.
    Node(SH + "basic_spine_000", "Fervour",
         (R(PER_KILL, "flat", 10, cond=RUNG_IS, cond_value=0),
          R(PER_KILL, "flat", 25, cond=RUNG_AT_LEAST, cond_value=1),
          R(DECAY, "flat", 10),
          R(GRACE, "flat", 4),
          R("fervour_per_critical_strike", "flat", 2)),
         before=((R(PER_KILL, "flat", 10, cond=RUNG_IS, cond_value=0),
                  R(PER_KILL, "flat", 25, cond=RUNG_AT_LEAST, cond_value=1),
                  R(DECAY, "flat", 10),
                  R(GRACE, "flat", 4)),)),
    Node(SH + "basic_spine_001", "Big Game", damage("increased", 2, cond=RUNG_AT_LEAST, cond_value=1)),
    Node(SH + "basic_spine_003", "Killing Pace",
         (R("attack_speed", "increased", 1, cond="seconds_after_kill", cond_value=4),
          R("movement_speed", "increased", 1, cond="seconds_after_kill", cond_value=4))),
    Node(SH + "basic_spine_004", "Reaper's Share", (R(PER_KILL, "increased", 2),)),
    Node(SH + "basic_spine_006", "Slow Exhale", (R(DECAY, "increased", -5),)),
    Node(SH + "keystone_spine_001", "Momentum", (R(GRACE, "flat", 2),)),
    Node(SH + "basic_spine_002", "Quick Recovery",
         (R("cooldown_reduction", "flat", 3, tags="Slot.Movement"),)),
    Node(SH + "basic_spine_005", "Held Breath", damage("increased", 1, scale=HELD, step=20)),
    Node(SH + "basic_spine_009", "Fresh Start",
         (R("attack_speed", "increased", 2, cond="seconds_after_movement_skill",
            cond_value=3),)),
    Node(SH + "basic_a_stem1", "Lingering", (R("dot_damage", "increased", 3),)),
    Node(SH + "basic_a_a0", "Deep Venom",
         (R("dot_damage", "increased", 2, tags="Keyword.DoT.Poison"),)),
    Node(SH + "basic_a_b0", "Open Veins",
         (R("dot_damage", "increased", 2, tags="Keyword.DoT.Bleed"),)),
    Node(SH + "basic_a_b1", "Arterial",
         (R("dot_frequency", "increased", 3, tags="Keyword.DoT.Bleed"),)),
    Node(SH + "basic_a_c0", "Slow Death",
         (R("dot_damage", "increased", 4, cond="target_is_boss"),)),
    Node(SH + "basic_b_stem0", "Keen Edge", (R("crit_chance", "increased", 3),)),
    Node(SH + "basic_b_stem1", "Find the Gap", (R("crit_multiplier", "increased", 4),)),
    Node(SH + "basic_b_stem2", "Read the Giant", damage("increased", 3, cond="target_is_boss")),
    Node(SH + "basic_b_a0", "Open the Throat",
         (R("crit_multiplier", "increased", 3, cond="target_health_below", cond_value=50),)),
    # HUNTER'S EYE'S FIRST HALF, against Elite and stronger enemies; its second
    # half, "against the enemy Deathmark marked", waits for the mark.
    Node(SH + "basic_b_b2", "Hunter's Eye",
         (R("crit_chance", "increased", 2, cond=RUNG_AT_LEAST, cond_value=1),)),
    Node(SH + "basic_b_c1", "Siege of One",
         (R(DECAY, "increased", -8, cond="seconds_after_striking_a_boss", cond_value=2),)),
    Node(SH + "basic_b_a2", "Last Breath",
         damage("increased", 3, cond="target_health_below", cond_value=30)),
    Node(SH + "keystone_b_kA", "Executioner",
         damage("more", 100, cond="target_health_below", cond_value=30)),
    # LIGHT FEET, "+1% increased Movement Speed per point, doubled for 4 seconds
    # after a kill": the same amount again inside the window.
    Node(SH + "basic_c_stem0", "Light Feet",
         (R("movement_speed", "increased", 1),
          R("movement_speed", "increased", 1, cond="seconds_after_kill", cond_value=4))),
    Node(SH + "basic_c_a0", "Cutting Path", damage("increased", 4, tags="Slot.Movement")),
    Node(SH + "basic_c_a2", "Blade on Arrival",
         (R("crit_chance", "increased", 3, tags="Slot.Movement"),)),
    Node(SH + "basic_c_b1", "Through and Through",
         tuple(R("fervour_per_enemy_hit", "flat", 1, tags="Slot.Movement", min_points=points)
               for points in (2, 4, 6, 8))),
    Node(SH + "basic_c_b2", "Deep Cut",
         (R("bleed_chance", "flat", 5, tags="Slot.Movement"),)),
    Node(SH + "basic_c_c1", "Fed Stride",
         (R("cooldown_reduction", "flat", 5, tags="Slot.Movement",
            cond="class_resource_points_at_least", cond_value=50),)),
    # COUNTERSTROKE'S FIRST HALF, "+1% Evasion chance per point": the flat evasion
    # every other Evasion node is reached through (each needs 4 points in it), so
    # Nimble's and Moving Target's increases always have a base under them. Its
    # second half needs the Slip keystone's critical strike and is not built.
    Node(SH + "basic_d_stem1", "Counterstroke", (R("evasion", "flat", 1),)),
    Node(SH + "basic_d_stem2", "Nimble", (R("evasion", "increased", 3),)),
    Node(SH + "basic_d_b0", "Moving Target",
         (R("evasion", "increased", 3, cond="while_moving"),)),
    Node(SH + "basic_d_b1", "Tumble",
         (R("evasion", "flat", 3, cond="seconds_after_movement_skill", cond_value=2),)),
    Node(SH + "basic_d_b2", "Slipstream",
         (R("movement_speed", "increased", 2, cond="seconds_after_dodge", cond_value=3),)),
    Node(SH + "basic_d_c0", "Untouched",
         tuple(R("evasion", "flat", 1, scale=HELD, step=20, min_points=points)
               for points in (2, 4, 6))),
    Node(SH + "capstone_25", "Quickening",
         (R("movement_speed", "increased", 3, scale=HELD, step=10),), option=3),
    Node(SH + "capstone_50", "Honed Edge",
         (R("crit_chance", "flat", 1, scale=HELD, step=10),), option=3),

    # ---- SLICE C1 (2026-11-25): THE SIXTEEN NODES AND OPTIONS TODAY'S VOCABULARY
    # STATES WHOLE, the passive row port's action columns included. Each was put
    # through the generator before it was written. Burnt Offering waits for C3's
    # record of Fervour spent: `resource_consumed`, its event today, also fires
    # for a next-use charge spent.
    Node(SC + "basic_spine_002", "Last Breath",
         tuple(R("health_restored_on_kill_at_no_cost", "flat", 1, min_points=points)
               for points in (2, 4, 6))),
    Node(SC + "basic_a_stem1", "Gathered Weight",
         (R("area_of_effect", "increased", 1, scale=HELD, step=40),)),
    # KILL CHAIN: a kill grants a stack and restarts the one window all the stacks
    # share (`UCataclysmAbilitySystemComponent::GrantOwnStack`), so they lapse
    # together 2 seconds after the last kill, which is "kills within 2 seconds of
    # each other form a chain". The four rows of a stat share one stack, granted
    # once a kill (`StackedThisEvent`).
    Node(SC + "basic_b_a0", "Kill Chain",
         tuple(row for points in (2, 4, 6, 8)
               for row in damage("increased", 1, scale=OWN, step=1, cap=10, event="kill",
                                 stack_seconds=2, min_points=points))),
    Node(SC + "capstone_50", "Second Wind",
         (A("health", 100, event="resource_full", fraction_of="maximum", trigger_cooldown=60),
          A("mana", 100, event="resource_full", fraction_of="maximum", trigger_cooldown=60)),
         option=1),
    Node(NE + "basic_spine_008", "Massed Dead",
         (R("minion_damage", "increased", 1, scale=MINIONS, step=5, cap=5),)),
    Node(NE + "basic_ch_c1", "Cheap Bones", (R("minion_reserve_percent_less", "flat", 4),)),
    Node(NE + "capstone_25", "Strength in Numbers",
         damage("more", 1, scale=MINIONS, step=1, cap=20)
         + (R("damage_taken", "more", -1, scale=MINIONS, step=1, cap=20),), option=3),
    # GATHERING DARK: "gain 2% area" read as increased, the bucket every other
    # "+N% area" in the trees is (a judgement).
    Node(NE + "capstone_50", "Gathering Dark",
         (R("area_of_effect", "increased", 2, scale=MINIONS, step=1),), option=3),
    Node(NE + "basic_gr_stem0", "Among the Dead",
         (R("max_health", "increased", 1),
          R("armor", "increased", 1, scale=MINIONS, step=5, cap=4))),
    # CARAPACE: "while you have 10 or more minions" as a count of minions in steps
    # of 10 capped at one step, which is nought below 10 and one step from 10 on.
    # No condition counts minions held (a judgement).
    Node(NE + "basic_gr_a2", "Carapace",
         (R("max_health", "increased", 2, scale=MINIONS, step=10, cap=1),)),
    Node(NE + "keystone_gr_kA", "Bone Armour",
         (R("damage_taken", "more", -1, scale=MINIONS, step=1, cap=20),)),
    # ENVENOM: the Bleed half is the stat every Bleed on a critical strike reads;
    # the Poison half applies Poison at its own size and duration on a critical
    # strike, by the chance the value states.
    Node(SH + "basic_spine_008", "Envenom",
         (R("bleed_on_crit_chance", "flat", 6),
          A("apply_status", 6, event="critical_strike", ailment="Poison", trigger_cooldown=0))),
    Node(SH + "basic_a_c1", "Bide",
         (A("apply_status", 3, event="hit_dealt", ailment="Poison", cond="target_is_boss",
            trigger_cooldown=0),)),
    Node(SH + "basic_b_b1", "Wearing Cut",
         tuple(row for points in (2, 4, 6, 8)
               for row in damage("increased", 1, scale=HITS, step=1, cap=10, event="hit_dealt",
                                 min_points=points))),
    Node(SH + "basic_c_a1", "Quick Kill",
         (R(PER_KILL, "flat", 1, cond="seconds_after_movement_skill", cond_value=2),)),
    # DEATH SENTENCE: the execute never takes a Boss or a Cataclysm Boss (the engine
    # holds that inside the action), and the double damage is for those two rungs.
    Node(SH + "capstone_200", "Death Sentence",
         (A("execute_target", 100, event="hit_dealt", cond="target_health_below", cond_value=15),)
         + damage("more", 100, cond="target_health_below", cond_value=15, cond2=RUNG_AT_LEAST,
                  cond_value2=4),
         option=2),

    # ---- SLICE C2 (2026-11-25): THE SOUL COLLECTOR'S SIPHON AND ITS LIMB, read by
    # `FCataclysmSiphon`. "Increased area of your Siphon" is an increase on its
    # radius, as every area in this game multiplies its radius.
    Node(SC + "basic_c_stem0", "The Siphon", (R("siphon_radius_metres", "flat", 6),)),
    Node(SC + "basic_c_stem1", "Sapping Cold",
         (R("siphon_enemies_deal_less_percent", "flat", 1),)
         + tuple(row for points in (2, 4, 6)
                 for row in (R("siphon_enemy_slow_percent", "flat", 3, min_points=points),)
                 + damage("increased", 1, scale=SIPHONED, step=1, cap=10, min_points=points)
                 + (R("movement_speed", "increased", 1, scale=SIPHONED, step=1, cap=10,
                      min_points=points),))),
    Node(SC + "basic_c_stem2", "Pall of Souls",
         (R("siphon_radius_metres", "increased", 1, scale=HELD, step=20),)),
    # LIFE SIPHON: "At 10 points: each enemy in your Siphon restores 1% of your
    # maximum health each second, up to 5 enemies", granted whole from 10 points.
    Node(SC + "basic_c_a0", "Life Siphon",
         (R("siphon_life_percent", "flat", 1, scale=SIPHONED, step=1, cap=5, min_points=10),)),
    Node(SC + "basic_c_a1", "Drain", (R("siphon_drain_weapon_percent", "flat", 2),)),
    Node(SC + "basic_c_a2", "Necrotic Siphon", (R("siphon_necrosis_chance", "flat", 5),)),
    Node(SC + "basic_c_b0", "Enfeeble", (R("siphon_enemies_deal_less_percent", "flat", 1),)),
    Node(SC + "basic_c_b1", "Stolen Strength",
         tuple(R("damage_reduction", "increased", 1, scale=SIPHONED, step=1, cap=10, min_points=points)
               for points in (4, 8))),
    Node(SC + "basic_c_b2", "Soul-Heavy Air",
         damage("increased", 1, cond="target_in_your_siphon", scale=HELD, step=50)),
    Node(SC + "basic_c_c0", "Lingering Pull",
         tuple(R("siphon_linger_seconds", "flat", 1, min_points=points) for points in (4, 8))),
    Node(SC + "basic_c_c1", "Reaching Cold", (R("siphon_radius_metres", "increased", 4),)),
    # LIVING HARVEST: "1 Fervour every 2 seconds, up to 4 Fervour a second", paid
    # once a second as half a Fervour for each enemy, up to 8 enemies.
    Node(SC + "keystone_c_kA", "Living Harvest",
         (R("siphon_fervour_per_second", "flat", 0.5, scale=SIPHONED, step=1, cap=8),)),

    # ---- SLICE C3a (2026-11-25): THE NODES THAT ARE ROWS ON THE RECORD OF FERVOUR
    # SPENT. Spent Breath's "10 or more Fervour at once" is the Event Value of its
    # own stack, which `fervour_spent` grants and a new spend renews; Quick
    # Kindling and Burnt Offering read the clock.
    Node(SC + "basic_d_stem0", "Spent Breath",
         damage("increased", 3, scale=OWN, step=1, cap=1, event="fervour_spent", stack_seconds=4,
                event_value=10)),
    Node(SC + "basic_d_stem2", "Quick Kindling",
         (R("cooldown_reduction", "flat", 1, cond="seconds_after_fervour_spent", cond_value=4),)),
    Node(SC + "basic_d_b0", "Burnt Offering",
         (R("damage_taken", "increased", -1, cond="seconds_after_fervour_spent", cond_value=4),)),

    # ---- SLICE C3b (2026-11-25): THE NODES SIZED BY WHAT ONE SPEND CARRIED, and
    # Bound Offering. "For each Fervour spent" and "for every 10 Fervour spent"
    # are the two Fraction Of values that multiply a row's value by the whole
    # units, or whole tens, of the spend; whole tens, so a spend of 19 pays once.
    Node(SC + "keystone_d_kB", "Feast",
         (A("health", 1, event="fervour_spent", fraction_of="for_each_of_event_amount"),)),
    Node(SC + "basic_d_b1", "Spirit to Mana",
         (A("mana", 1, event="fervour_spent", fraction_of="for_each_ten_of_event_amount"),)),
    Node(SC + "basic_d_b2", "Ashen Ward",
         (A("energy_shield", 1, event="fervour_spent", fraction_of="for_each_ten_of_event_amount"),)),
    # SOUL FURNACE: "Spending 10 or more Fervour at once releases a blast ... 10%
    # of your weapon damage for each Fervour spent to every enemy within 5
    # metres": a smite on everything within the five metres every nearby action
    # reaches, its Event Value the ten.
    Node(SC + "keystone_d_kA", "Soul Furnace",
         (A("smite_nearby", 10, event="fervour_spent", fraction_of="for_each_of_event_amount",
            event_value=10),)),
    Node(SC + "keystone_d_kC", "Turn of the Wheel",
         (A("cooldown_reduce_others", 1, event="fervour_spent", fraction_of="for_each_ten_of_event_amount"),)),
    Node(SC + "basic_d_c0", "Bound Offering", (R("reserved_fervour_spent_percent", "flat", 15),)),

    # ---- SLICE C3c (2026-11-25): A SHARE OF A SPEND RETURNED, A LETHAL BLOW SAVED BY
    # FERVOUR, AND A MOVEMENT SKILL USED ON COOLDOWN FOR FERVOUR. The 4 seconds of
    # Cinders Kept and the 50 of Phylactery are the engine's
    # (`FervourReturnedAfterSeconds`, `FervourLethalSaveLeast`); Phylactery's 60 is
    # its row. Borrowed Step's "40 Fervour, reduced by 2 per point" is two rows on
    # one stat: 40 once from the first point, and -2 a point.
    Node(SC + "basic_d_c1", "Cinders Kept", (R("fervour_spent_returned_percent", "flat", 3),)),
    Node(SC + "capstone_100", "Phylactery",
         (R("fervour_lethal_save_every_seconds", "flat", 60),), option=1),
    Node(SH + "basic_c_c0", "Borrowed Step",
         (R("movement_on_cooldown_fervour_cost", "flat", 40, min_points=1),
          R("movement_on_cooldown_fervour_cost", "flat", -2))),

    # ---- SLICE C3d (2026-11-25): THE SOUL COLLECTOR'S FOUR FERVOUR THRESHOLDS.
    # Each vow's clause is a row with no option on its capstone, accepted because
    # the capstone's own sentence states it (the coordinating session's ruling 4),
    # under `fervour_threshold_reached` with its figure. "Cost no mana" is a
    # removal of `mana_cost`, as the Ritualist's keystone writes it; "recover twice
    # as fast" is 100 on the rate the regeneration step applies. The fourth vow's
    # clause and the second vow's chain half are recorded in docs/DECISIONS.md.
    Node(SC + "capstone_25", "The First Vow",
         (R("area_of_effect", "more", 20, cond="fervour_threshold_reached", cond_value=20),)),
    Node(SC + "capstone_50", "The Second Vow",
         (R("projectile_count", "flat", 1, cond="fervour_threshold_reached", cond_value=40),)),
    Node(SC + "capstone_100", "The Third Vow",
         (R("mana_cost", "removed", 1, cond="fervour_threshold_reached", cond_value=60),)),
    Node(SC + "capstone_200", "The Final Vow",
         (R("cooldowns_recover_faster_percent", "flat", 100, cond="fervour_threshold_reached", cond_value=100),)),
    Node(SC + "basic_a_c0", "Generous Count", (R("fervour_threshold_reduction", "flat", 1),)),
    Node(SC + "basic_a_c1", "Steady Count", damage("increased", 1, scale="fervour_thresholds_reached", step=1)),
    Node(SC + "keystone_a_kC", "Long Shadow", (R("fervour_threshold_linger_seconds", "flat", 5),)),

    # ---- SLICE C3e (2026-11-25): EMPTY HANDS AND CHAINED SOULS. Empty Hands reads
    # the Fervour counted as held, which Chained Souls raises by its share of what
    # followers hold back; the full bar and Phylactery's spend read the pool alone.
    Node(SC + "basic_b_stem1", "Empty Hands",
         (R("attack_speed", "increased", 2, cond="class_resource_points_below", cond_value=20),)),
    Node(SC + "basic_a_stem2", "Chained Souls", (R("reserved_fervour_counts_as_held_percent", "flat", 8),)),

    # ---- SLICE C4a (2026-11-25): THE FIRST NODES THAT ACT ON A KILL. Soul Tide reads
    # the kill clock before the kill stamps it, so "within 2 seconds of each other"
    # is the time since the previous kill; Thinning the Herd counts the living
    # enemies within 8 metres, the slain one already dead; Clean Kill's "for every
    # 2 points" is a stat row at each second point, read on a kill whose killing
    # blow was a critical strike.
    # SOUL TIDE MOVED TO `kill_chain_within_seconds` IN SLICE C6c (2026-11-25), as
    # ruled, so Unbroken Chain's "The same applies to Soul Tide" lengthens it.
    Node(SC + "capstone_25", "Soul Tide",
         (R("fervour_per_kill", "flat", 1, cond="kill_chain_within_seconds", cond_value=2),), option=1,
         before=((R("fervour_per_kill", "flat", 1, cond="seconds_after_kill", cond_value=2),),)),
    Node(SC + "basic_b_stem0", "Gleaning", (R("fervour_per_kill_extra_chance", "flat", 5),)),
    Node(SC + "basic_b_stem2", "Thinning the Herd",
         (R("fervour_per_kill", "increased", 1, scale="enemies_in_reach", step=1, reach=8, cap=10),)),
    Node(SH + "basic_a_stem0", "Death's Patience",
         (R("fervour_per_kill", "flat", 1, cond="target_carries_your_poison_or_bleed"),)),
    Node(SH + "keystone_c_kA", "Flicker", (A("cooldown_reset_movement", 100, event="kill"),)),
    Node(SH + "basic_b_a1", "Clean Kill",
         tuple(R("health_restored_on_critical_kill", "flat", 1, min_points=points) for points in (2, 4, 6, 8))),
    Node(SH + "basic_c_stem2", "Hunting Leap",
         (R("skill_range", "increased", 3, tags="Slot.Movement"),
          R("skill_range", "increased", 3, tags="Slot.Movement", cond="seconds_after_kill", cond_value=2))),

    # ---- SLICE C4b1 (2026-11-25): FERVOUR FROM OTHER SOURCES AND THE HOLD BEFORE
    # DECAY. Every gain reaches the pool through `UCataclysmFervour::RaisePool`;
    # Any Fuel is a row on the rate every gain is scaled by, under
    # `fervour_gain_not_from_a_kill`; Kindred Fires restarts the hold on any rise;
    # Borrowed Time lengthens a running hold on a rise that is not a kill's, a row
    # at each third point.
    Node(SC + "basic_b_c0", "Any Fuel",
         (R("class_resource_generation", "increased", 2, cond="fervour_gain_not_from_a_kill"),)),
    Node(SC + "basic_b_c1", "Borrowed Time",
         tuple(R("fervour_hold_extended_on_gain_seconds", "flat", 1, min_points=points) for points in (3, 6))),
    Node(SC + "keystone_b_kC", "Kindred Fires", (R("fervour_hold_restarted_by_any_gain", "flat", 1),)),

    # ---- SLICE C4b2 (2026-11-25): DEATHS THE SOUL COLLECTOR DID NOT MAKE, AND DECAY
    # GIVEN BACK. Reclaimed gives back what either decay took in the 3 seconds
    # before a kill; Claimed pays an enemy's Fervour when it dies within a second
    # for every 2 points of your hit, a row at each second point; Death's Toll makes
    # a follower's kill yours for this tree, its `kill` raised to this tree's rows.
    Node(SC + "keystone_spine_002", "Reclaimed", (R("decay_given_back_on_kill_seconds", "flat", 3),)),
    Node(SC + "basic_spine_007", "Claimed",
         tuple(R("fervour_claimed_within_seconds", "flat", 1, min_points=points) for points in (2, 4, 6, 8))),
    Node(SC + "keystone_spine_001", "Death's Toll", (R("follower_kills_count_as_yours", "flat", 1),)),

    # ---- SLICE C4c1 (2026-11-25): EXPLOSIONS AT A KILL. Passed On is the existing
    # overkill explosion at half, only for a kill by a hit, with its own radius,
    # the larger of its 3 metres and any other source's; The Ferryman is a burst
    # of 10% of the slain creature's maximum health while the bar is full.
    Node(SC + "keystone_b_kA", "Passed On",
         (A("explode_victim_for_overkill", 50, event="kill", cond="killing_blow_was_a_hit"),
          R("overkill_explosion_radius_metres", "flat", 3))),
    Node(SC + "capstone_200", "The Ferryman",
         (A("burst_slain_for_share_of_maximum_health", 10, event="kill", cond="class_resource_at_maximum"),),
         option=2),

    # ---- SLICE C4c2 (2026-11-25): MARKS LEFT BY A DEATH. Grim Wake marks the
    # enemies within its rows' 3 metres of a kill of yours, a follower's under
    # Death's Toll included, and your damage to a marked enemy is raised for 3
    # seconds; Blood Debt passes your Bleed left on an enemy that died to the
    # strongest enemy within 10 metres, over 2 seconds. Its Deathmark half is not
    # built.
    Node(SC + "basic_b_a2", "Grim Wake",
         damage("increased", 2, cond="target_near_your_kill_within_seconds", cond_value=3, reach=3)),
    Node(SH + "keystone_a_kB", "Blood Debt",
         (A("pass_bleed_left_to_strongest", 2, event="afflicted_death", tags="Keyword.DoT.Bleed"),)),

    # ---- SLICE C4d1 (2026-11-25): CRITICAL STRIKES, EXECUTES AND KILLS THAT SPEED
    # THE NEXT ONE. Telling Blows pays Fervour for a critical strike on an Elite
    # or stronger; Reaper's Edge executes a non-Boss below 15% and doubles that
    # kill's Fervour; Relentless takes a second off every cooldown at a kill for
    # each whole 100 Fervour held; Chain Execution makes the next landed hit
    # within 2 seconds of a kill a critical strike.
    Node(SH + "basic_spine_007", "Telling Blows",
         (R("fervour_per_critical_strike", "flat", 1, cond=RUNG_AT_LEAST, cond_value=1),)),
    Node(SC + "capstone_100", "Reaper's Edge",
         (A("execute_target", 100, event="hit_dealt", cond="target_health_below", cond_value=15),
          R(PER_KILL, "more", 100, cond="kill_was_an_execute")),
         option=2),
    Node(SH + "capstone_100", "Relentless",
         (A("cooldown_reduce_all", 1, event="kill", fraction_of="for_each_hundred_class_resource_held"),),
         option=3),
    Node(SH + "capstone_100", "Chain Execution", (A("next_hit_critical", 2, event="kill"),), option=2),

    # ---- SLICE C4d2 (2026-11-25): A BOSS FIGHT FILLS THE BAR WITHOUT KILLS.
    # Patient Blade pays one kill's Fervour of a Boss for a tick of your damage
    # over time on it, once every 2 seconds; Death by Inches counts every 10th
    # hit you land on one Boss as a kill of it, everything that needs a body
    # left out.
    Node(SH + "keystone_a_kC", "Patient Blade",
         (R("boss_ticks_pay_a_kill_every_seconds", "flat", 2, cond="target_is_boss"),)),
    Node(SH + "keystone_b_kC", "Death by Inches",
         (R("boss_hits_counted_as_a_kill_every", "flat", 10, cond="target_is_boss"),)),
    Node(SC + "capstone_200", "Wearer of the Dead",
         (R("max_health", "more", 50, cond="wearing_enemy_modifier", cond_value=1),
          R("crit_chance", "flat", 100, cond="wearing_enemy_modifier", cond_value=2),
          R("life_leech", "flat", 10, cond="wearing_enemy_modifier", cond_value=3),
          R("crowd_control_resistance", "flat", 100, cond="wearing_enemy_modifier", cond_value=4),
          R("health_regen", "flat", 2, cond="wearing_enemy_modifier", cond_value=5)),
         option=1),

    # ---- SLICE C6a (2026-11-25): EVASIONS AND A FULL BAR THAT SPEED THE NEXT
    # THING, AND FOUR NODES THAT ARE DATA ALONE. Slip and Brim's next-hit critical
    # strike on `dodge` and `resource_full` (Brim's until used, a judgement);
    # Find the Rhythm's and Sidestep's movement cooldown cut and Keep the Count's
    # longer hold, each a row at each step of "for every N points"; Soul Barrage,
    # Lasting Wounds, Twin Step, and Riposte as a melee strike on `dodge`, the
    # basic attack's stand-in, as ruled.
    Node(SH + "basic_spine_010", "Brim",
         (A("next_hit_critical", 0, event="resource_full"),)
         + damage("increased", 5, cond="next_hit_is_critical")),
    Node(SH + "basic_d_stem0", "Slip", (A("next_hit_critical", 3, event="dodge"),)),
    Node(SH + "basic_b_c0", "Find the Rhythm",
         tuple(A("cooldown_reduce_movement", 1, event="critical_strike", cond="target_is_boss", min_points=points)
               for points in (4, 8))),
    Node(SH + "keystone_d_kC", "Sidestep",
         tuple(A("cooldown_reduce_movement", 1, event="dodge", min_points=points) for points in (5, 10))),
    Node(SH + "basic_d_c1", "Keep the Count",
         tuple(A("fervour_hold_extended", 1, event="dodge", min_points=points) for points in (3, 6))),
    Node(SC + "capstone_200", "Soul Barrage",
         (R("projectile_count", "flat", 1, scale=HELD, step=10),), option=3),
    Node(SH + "basic_a_stem2", "Lasting Wounds",
         (R("dot_duration", "increased", 4, tags="Keyword.DoT.Poison"),
          R("dot_duration", "increased", 4, tags="Keyword.DoT.Bleed"))),
    Node(SH + "keystone_c_kC", "Twin Step", (R("skill_charges_bonus", "flat", 1, tags="Slot.Movement"),)),
    Node(SH + "keystone_d_kA", "Riposte", (A("strike_target", 100, event="dodge", tags="Type.Melee"),)),

    # ---- SLICE C6b1 (2026-11-25): FACTS OF THE TARGET OF A HIT. From Behind's
    # crit chance on a hit from behind; Opening Strike's critical strike (a
    # chance of 100, held under the ceiling, as Overpowered's is) and 50% more on
    # a first hit at full health; Venomed Prey's damage on your Poison; Hit and
    # Run's Bleed damage beyond 4 metres, judged at each tick.
    Node(SH + "basic_b_b0", "From Behind",
         (R("crit_chance", "increased", 3, cond="target_struck_from_behind"),)),
    Node(SH + "capstone_25", "Opening Strike",
         (R("crit_chance", "flat", 100, cond="target_not_yet_struck_by_you", cond2="target_at_full_health"),)
         + damage("more", 50, cond="target_not_yet_struck_by_you", cond2="target_at_full_health"),
         option=1),
    Node(SH + "basic_a_a2", "Venomed Prey", damage("increased", 2, cond="target_carries_your_poison")),
    Node(SH + "basic_a_b2", "Hit and Run",
         (R("dot_damage", "increased", 3, tags="Keyword.DoT.Bleed", cond="target_beyond_metres", cond_value=4),)),

    # ---- SLICE C6b2 (2026-11-25): THE MOVEMENT SKILL'S TWO CLOCKS. Arrival's
    # damage for 1 second after a movement skill that was paid for ENDS; Glancing
    # Cuts' damage against an enemy your movement skill's landed blow struck in
    # the last 4 seconds, a record kept on that enemy.
    Node(SH + "basic_c_stem1", "Arrival",
         damage("increased", 3, cond="seconds_after_movement_skill_ends", cond_value=1)),
    Node(SH + "basic_c_b0", "Glancing Cuts",
         damage("increased", 2, cond="target_struck_by_your_movement_skill_within_seconds", cond_value=4)),

    # ---- SLICE C6b3 (2026-11-25): FERVOUR ON EVASIONS AND HITS ON THE STRONG, AND
    # DECAY HELD BY WHO IS NEAR. A flat amount of Fervour is an action row on
    # `class_resource` with Fraction Of `points`, as ruled. Opening Found pays 1 a
    # point on an evasion and 1 more when the attacker is Elite or stronger; Reaper's
    # Due 5 on a hit on a Boss, at most twice a second, and removes decay (the
    # `removed` kind, since a Less multiplier stops at 99%) while a Boss is within 15
    # metres; Tear Loose rolls 2% a point for 1 on a hit on an Elite or
    # stronger. Read the Blow raises evasion against an Elite's attacks; Hunting
    # Ground removes decay while no enemy is within 20 metres; Vigil Over the Strong
    # slows it 5% a point while an Elite or stronger is within 15.
    Node(SH + "basic_d_a2", "Opening Found",
         (A("class_resource", 1, event="dodge", fraction_of="points"),
          A("class_resource", 1, event="dodge", fraction_of="points", cond=RUNG_AT_LEAST, cond_value=1))),
    Node(SC + "capstone_25", "Reaper's Due",
         (A("class_resource", 5, event="hit_dealt", fraction_of="points", cond="target_is_boss",
            trigger_cooldown=0.5),
          R(DECAY, "removed", 1, cond="enemy_of_rarity_in_reach", cond_value=4, reach=15)),
         option=2),
    Node(SC + "basic_b_b1", "Tear Loose",
         (A("class_resource", 1, event="hit_dealt", fraction_of="points", cond=RUNG_AT_LEAST, cond_value=1,
            chance_percent=2),)),
    Node(SH + "basic_d_a0", "Read the Blow",
         (R("evasion", "flat", 1, cond="opponent_rarity_at_least", cond_value=1),)),
    Node(SH + "keystone_spine_002", "Hunting Ground",
         (R(DECAY, "removed", 1, cond="no_enemy_in_reach", reach=20),)),
    Node(SC + "basic_b_b2", "Vigil Over the Strong",
         (R(DECAY, "increased", -5, cond="enemy_of_rarity_in_reach", cond_value=1, reach=15),)),

    # ---- SLICE C6b4 (2026-11-25): A POISON THAT LASTS, AND AN EVASION ANSWERED
    # TWICE. Festering's Poison damage rises 1% for each second the enemy has
    # stayed poisoned by you without a break, up to 10, at every 2nd point; Twice
    # Answered rolls 5% a point on each evasion for the critical strike charge
    # that evasion grants to hold two, as ruled.
    Node(SH + "basic_a_a1", "Festering",
         tuple(R("dot_damage", "increased", 1, tags="Keyword.DoT.Poison", scale="seconds_poisoned_by_you", step=1,
                 cap=10, min_points=points) for points in (2, 4, 6, 8, 10))),
    Node(SH + "basic_d_a1", "Twice Answered",
         (A("next_hit_critical_twice", 2, event="dodge", chance_percent=5),)),

    # ---- SLICE C6c (2026-11-25): HOW FERVOUR IS LOST, AND WHAT FILLS PAST THE BAR,
    # as ruled. Miser's Grip's floor stops the kill decay only; Soulbound removes the
    # decay and takes 1 Fervour for every 2% of maximum health lost to damage;
    # Overflow turns each whole Fervour above the full bar into a stack of 1%
    # attack speed sharing one 5 second window, up to 30; Unbroken Chain adds 1
    # second to a chain of kills at 4 and at 8 points, for Kill Chain's stacks and
    # Soul Tide's window.
    Node(SC + "basic_a_b1", "Miser's Grip", (R("fervour_decay_floor_percent", "flat", 3),)),
    Node(SC + "keystone_spine_003", "Soulbound",
         (R(DECAY, "removed", 1), R("health_percent_lost_per_fervour_lost", "flat", 2))),
    Node(SC + "keystone_a_kA", "Overflow",
         (R("fervour_overflow_attack_speed_seconds", "flat", 5),
          R("attack_speed", "increased", 1, scale="fervour_overflow_stacks", step=1, cap=30))),
    Node(SC + "basic_b_a1", "Unbroken Chain",
         tuple(R("kill_chain_seconds", "flat", 1, min_points=points) for points in (4, 8))),

    # ---- SLICE C6d1 (2026-11-25): THE NECROMANCER'S MINIONS AROUND HIM AND THEIR
    # BLOWS, as ruled. Four nodes read how many of your own minions stand within 6
    # metres (`minions_in_reach_at_least`); Cold Hands and Grave Cold act on each
    # landed minion blow (`minion_hit`), with no wait between, and Grave Cold's
    # minions deal more to an enemy carrying Necrosis; Hasty Dead and Grave Pact are
    # new minion stats read from the summoner.
    Node(NE + "basic_spine_005", "Gravekeeper's Hide",
         (R("max_health", "increased", 1),
          R("armor", "increased", 1, cond="minions_in_reach_at_least", cond_value=3, reach=6))),
    Node(NE + "basic_gr_a0", "Shielding Ranks",
         (R("damage_reduction", "increased", 1, cond="minions_in_reach_at_least", cond_value=5, reach=6),)),
    Node(NE + "basic_gr_c0", "Steadfast Among Them",
         (R("crowd_control_resistance", "increased", 2),
          R("crowd_control_resistance", "increased", 2, cond="minions_in_reach_at_least", cond_value=5, reach=6))),
    Node(NE + "keystone_gr_kC", "Ringed in Bone",
         (R("crowd_control_resistance", "flat", 100, cond="minions_in_reach_at_least", cond_value=5, reach=6),
          R("immune_to_slow", "flat", 1, cond="minions_in_reach_at_least", cond_value=5, reach=6))),
    Node(NE + "basic_spine_004", "Cold Hands",
         (A("apply_status", 4, event="minion_hit", ailment="Necrosis", trigger_cooldown=0),)),
    Node(NE + "keystone_spine_002", "Grave Cold",
         (A("apply_status", 100, event="minion_hit", ailment="Necrosis", trigger_cooldown=0),
          R("minion_damage", "more", 15, cond="target_carries_necrosis"))),
    Node(NE + "basic_ch_b0", "Hasty Dead",
         (R("minion_attack_speed", "increased", 2), R("minion_movement_speed", "increased", 2))),
    Node(NE + "keystone_lg_kC", "Grave Pact", (R("minion_damage_taken", "more", -30),)),

    # ---- SLICE C6d2 (2026-11-25): WHEN THE NECROMANCER'S MINIONS DIE, AND WHEN HE
    # IS HURT, as ruled. A follower killed raises `minion_death` on him and starts
    # two clocks: `seconds_after_minion_death` (Bone Knit) and
    # `minion_death_in_reach_within_seconds`, which also needs the death within
    # the row's reach (Death Ward). Marrow Feast heals on each death, at most ten
    # times a second (Q3). Avenging Dead's stack is granted only by a hit that
    # took at least 5% of maximum health (Q2), an event condition.
    Node(NE + "basic_gr_b1", "Death Ward",
         (R("damage_reduction", "increased", 2, cond="minion_death_in_reach_within_seconds", cond_value=3,
            reach=6),)),
    Node(NE + "basic_gr_b2", "Bone Knit",
         (R("health_regen", "increased", 3, cond="seconds_after_minion_death", cond_value=3),)),
    Node(NE + "keystone_gr_kB", "Marrow Feast",
         (A("health", 1, event="minion_death", fraction_of="maximum", trigger_cooldown=0.1),)),
    Node(NE + "basic_gr_stem2", "Avenging Dead",
         (R("minion_attack_speed", "increased", 3, cond="hit_took_at_least_percent_of_max_health", cond_value=5,
            scale="own_stacks", step=1, cap=1, event="hit_taken", stack_seconds=4),)),
)

#: The three Death class stat lines: (class, stat, level 1 base, gain per level).
#: Everything a class does not name falls back to the `Default` line. See the
#: 2026-11-24 entry in `docs/DECISIONS.md` for the reasoning and the sources.
CLASS_STATS: tuple[tuple[str, str, float, float], ...] = (
    ("Soul Collector", "max_health", 115, 17),
    ("Soul Collector", "health_regen", 2, 0.25),
    ("Soul Collector", "damage_reduction", 3, 0.04),
    ("Soul Collector", "crowd_control_resistance", 3, 0.08),
    ("Soul Collector", "movement_speed", 4.2, 0),
    ("Necromancer", "max_health", 85, 12),
    ("Necromancer", "max_mana", 70, 9),
    ("Necromancer", "mana_regen", 1.5, 0.18),
    ("Necromancer", "armor", 15, 2),
    ("Necromancer", "damage_reduction", 2, 0.03),
    ("Necromancer", "crowd_control_resistance", 5, 0.1),
    ("Necromancer", "movement_speed", 3.8, 0),
    ("Shadow", "max_health", 90, 13),
    ("Shadow", "crit_multiplier", 160, 0),
    ("Shadow", "area_of_effect", 90, 0),
    ("Shadow", "movement_speed", 4.4, 0),
)


# --------------------------------------------------------------------------
# Reading and writing
# --------------------------------------------------------------------------

def _number(value):
    if value is None or (isinstance(value, str) and not value.strip()):
        return None
    number = float(value)
    return int(number) if number.is_integer() else number


def _text(value):
    if value is None:
        return None
    text = str(value).strip()
    return text or None


def _out(value):
    if value is None:
        return None
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return value


def row_from_cells(cells: dict[str, object]) -> Row:
    optional = {field: (_text(cells.get(column)) if field in TEXT_FIELDS
                        else _number(cells.get(column)))
                for column, field in OPTIONAL_EFFECT_COLUMNS}
    return Row(stat=_text(cells.get("Stat")),
               kind=(_text(cells.get("Value Kind")) or "").lower() or None,
               value=_number(cells.get("Value Per Point")) or 0,
               tags=_text(cells.get("Required Tags")), cond=_text(cells.get("Condition")),
               cond_value=_number(cells.get("Condition Value")),
               scale=_text(cells.get("Scale")), step=_number(cells.get("Scale Step")),
               reach=_number(cells.get("Reach Metres")),
               min_points=_number(cells.get("Min Points")) or None, **optional)


def cells_from_row(node: Node, row: Row) -> dict[str, object]:
    cells = {"Node": node.key, "Stat": row.stat, "Value Kind": row.kind,
             "Value Per Point": _out(row.value), "Required Tags": row.tags,
             "Condition": row.cond, "Condition Value": _out(row.cond_value),
             "Scale": row.scale, "Scale Step": _out(row.step),
             "Option": node.option or None, "Node Name": node.label,
             "Reach Metres": _out(row.reach), "Min Points": _out(row.min_points)}
    for column, field in OPTIONAL_EFFECT_COLUMNS:
        cells[column] = _out(getattr(row, field))
    return cells


def _multiset(rows) -> collections.Counter:
    return collections.Counter(row.key() for row in rows)


def refuse_what_a_save_would_lose(book) -> None:
    formulas = sum(1 for sheet in book.worksheets for row in sheet.iter_rows() for cell in row
                   if isinstance(cell.value, str) and cell.value.startswith("="))
    charts = sum(len(sheet._charts) for sheet in book.worksheets)
    images = sum(len(sheet._images) for sheet in book.worksheets)
    if formulas or charts or images:
        raise ReapplyError(f"the workbook holds {formulas} formula(s), {charts} chart(s) and "
                           f"{images} image(s), which an openpyxl save can lose without saying "
                           f"so; make the edit by hand in Excel instead")


def _snapshot(sheet) -> dict:
    return {(cell.row, cell.column): cell.value for row in sheet.iter_rows() for cell in row
            if cell.value is not None}


def _read(sheet, required: tuple[str, ...]) -> tuple[list[str], list[dict[str, object]]]:
    header = [_text(cell.value) for cell in sheet[1]]
    while header and header[-1] is None:
        header.pop()
    missing = [c for c in required if c not in header]
    if missing:
        raise ReapplyError(f"{sheet.title!r} has no column(s) {missing}; its header is {header}")
    if len(set(header)) != len(header):
        raise ReapplyError(f"{sheet.title!r} names a column twice: {header}")
    rows: list[dict[str, object]] = []
    for values in sheet.iter_rows(min_row=2, max_col=len(header), values_only=True):
        cells = dict(zip(header, values, strict=False))
        if all(v is None or (isinstance(v, str) and not v.strip()) for v in values):
            cells = {}
        rows.append(cells)
    # TRAILING BLANK ROWS ARE NOT ROWS. openpyxl reports a sheet's used range, which
    # can run past the last row anybody wrote.
    while rows and not rows[-1]:
        rows.pop()
    return header, rows


@dataclasses.dataclass
class SheetPlan:
    header: list[str]
    old: list[dict[str, object]]
    new: list[dict[str, object]]
    report: list[str]
    changed: int


def plan_effects(sheet, nodes: tuple[Node, ...] = NODES) -> SheetPlan:
    """Work out the Passive Effects sheet's new rows. Raises ReapplyError on a node
    holding rows this script did not write."""
    header, old = _read(sheet, EFFECT_COLUMNS)
    report: list[str] = []
    added: list[dict[str, object]] = []
    changed = 0
    # THE COLUMNS THE SHEET LACKS, after its last one. Every row already there
    # keeps its cells and leaves the new ones empty, which is what an empty cell
    # always meant to the generator.
    missing = [column for column, _ in OPTIONAL_EFFECT_COLUMNS if column not in header]
    if missing:
        header = header + missing
        changed += 1
        report.append(f"{sheet.title!r}: added the column(s) {missing} after its last one")
    seen: set[tuple[str, int]] = set()
    replace: dict[int, list[dict[str, object]]] = {}
    drop: set[int] = set()
    for node in nodes:
        if not node.key.startswith(DEATH_PREFIXES):
            raise ReapplyError(f"{node.key} is not a Death node; this script writes Death rows only")
        if (node.key, node.option) in seen:
            raise ReapplyError(f"{node.key} option {node.option} is listed twice")
        seen.add((node.key, node.option))
        where = node.key + (f" option {node.option}" if node.option else "")
        indices = [index for index, cells in enumerate(old)
                   if _text(cells.get("Node")) == node.key
                   and (_number(cells.get("Option")) or 0) == node.option]
        current = [row_from_cells(old[index]) for index in indices]
        if current and _multiset(current) == _multiset(node.rows):
            continue
        # AN EARLIER STATE THIS SCRIPT WROTE IS REWRITTEN IN PLACE, at the node's
        # first row, the rest dropped. See `Node.before`.
        if current and any(_multiset(current) == _multiset(state) for state in node.before):
            changed += 1
            replace[indices[0]] = [cells_from_row(node, row) for row in node.rows]
            drop.update(indices[1:])
            report.append(f"{where} ({node.label}): {len(current)} row(s) out, {len(node.rows)} in")
            for row in current:
                report.append(f"    - {_describe(row)}")
            for row in node.rows:
                report.append(f"    + {_describe(row)}")
            continue
        if current:
            raise ReapplyError(f"{where} already holds {[r.key() for r in current]}, which is "
                               f"not what this script writes; somebody has changed them, so a "
                               f"person has to look")
        changed += 1
        added.extend(cells_from_row(node, row) for row in node.rows)
        report.append(f"{where} ({node.label}): {len(node.rows)} row(s) added")
        for row in node.rows:
            report.append(f"    + {_describe(row)}")
    new: list[dict[str, object]] = []
    for index, cells in enumerate(old):
        if index in replace:
            new.extend(replace[index])
        elif index not in drop:
            new.append(cells)
    return SheetPlan(header, old, new + added, report, changed)


def plan_class_stats(sheet, lines=CLASS_STATS) -> SheetPlan:
    """Work out the Class Stats sheet's new rows, refusing a Death class stat that
    already holds other numbers."""
    header, old = _read(sheet, CLASS_COLUMNS)
    report: list[str] = []
    added: list[dict[str, object]] = []
    changed = 0
    seen: set[tuple[str, str]] = set()
    for class_name, stat, base, per_level in lines:
        if class_name not in DEATH_CLASSES:
            raise ReapplyError(f"{class_name} is not a Death class; this script writes Death lines only")
        if (class_name, stat) in seen:
            raise ReapplyError(f"{class_name} {stat} is listed twice")
        seen.add((class_name, stat))
        current = [cells for cells in old
                   if _text(cells.get("Class")) == class_name and _text(cells.get("Stat")) == stat]
        wanted = (float(base), float(per_level))
        if len(current) == 1 and (float(_number(current[0].get("Base")) or 0),
                                  float(_number(current[0].get("Per Level")) or 0)) == wanted:
            continue
        if current:
            raise ReapplyError(f"{class_name} {stat} already holds "
                               f"{[(c.get('Base'), c.get('Per Level')) for c in current]}, not "
                               f"{wanted}; somebody has changed it, so a person has to look")
        changed += 1
        added.append({"Class": class_name, "Stat": stat, "Base": _out(float(base)),
                      "Per Level": _out(float(per_level))})
        report.append(f"{class_name} {stat}: base {base:g}, {per_level:g} a level, added")
    return SheetPlan(header, old, old + added, report, changed)


def _describe(row: Row) -> str:
    parts = [row.stat or f"action={row.action}", row.kind or "-", f"{row.value:g}"]
    names = ("tags", "cond", "cond_value", "scale", "step", "reach", "min_points") + tuple(
        field for _, field in OPTIONAL_EFFECT_COLUMNS if field != "action")
    for name in names:
        value = getattr(row, name)
        if value is not None:
            parts.append(f"{name}={value:g}" if isinstance(value, (int, float))
                         else f"{name}={value}")
    return " | ".join(parts)


def _write(sheet, the_plan: SheetPlan) -> None:
    for column, name in enumerate(the_plan.header, start=1):
        sheet.cell(1, column).value = name
    for offset, cells in enumerate(the_plan.new):
        for column, name in enumerate(the_plan.header, start=1):
            sheet.cell(offset + 2, column).value = cells.get(name)


def _read_back(book, the_plan: SheetPlan, title: str) -> None:
    written = book[title]
    header = the_plan.header
    if [_text(c.value) for c in written[1]][:len(header)] != header:
        raise ReapplyError(f"{title!r}'s header did not read back as {header}")
    rows = [dict(zip(header, values, strict=False))
            for values in written.iter_rows(min_row=2, max_col=len(header), values_only=True)]
    while rows and all(v is None for v in rows[-1].values()):
        rows.pop()
    intended = [{name: cells.get(name) for name in header} for cells in the_plan.new]
    if rows != intended:
        raise ReapplyError(f"{title!r} did not read back as intended ({len(rows)} rows read, "
                           f"{len(intended)} intended)")


def apply(path: pathlib.Path = WORKBOOK, check_only: bool = False) -> list[str]:
    """Add the Death rows to the workbook at `path`; return the report."""
    book = openpyxl.load_workbook(path)
    refuse_what_a_save_would_lose(book)
    effects = plan_effects(book[EFFECTS_SHEET])
    classes = plan_class_stats(book[CLASS_SHEET])
    changed = effects.changed + classes.changed
    if not changed:
        return ["every Death row is already as this script writes it; nothing written"]
    report = effects.report + classes.report
    if check_only:
        return report + [f"{changed} change(s) to make; nothing written (--check)"]

    written = (EFFECTS_SHEET, CLASS_SHEET)
    others = {s.title: _snapshot(s) for s in book.worksheets if s.title not in written}
    _write(book[EFFECTS_SHEET], effects)
    _write(book[CLASS_SHEET], classes)
    book.save(path)

    # READ BACK FROM DISK: the other sheets as they were, and these two exactly as planned.
    after = openpyxl.load_workbook(path)
    if [s.title for s in after.worksheets] != [s.title for s in book.worksheets]:
        raise ReapplyError("the sheets changed order or name on saving")
    for title, cells in others.items():
        if _snapshot(after[title]) != cells:
            raise ReapplyError(f"the sheet {title!r} changed on saving, and only {written} "
                               f"were written")
    _read_back(after, effects, EFFECTS_SHEET)
    _read_back(after, classes, CLASS_SHEET)
    return report + [f"wrote {changed} change(s); {EFFECTS_SHEET!r} now holds "
                     f"{len(effects.new)} rows (was {len(effects.old)}) and {CLASS_SHEET!r} "
                     f"{len(classes.new)} (was {len(classes.old)}), and no other sheet changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--check", action="store_true",
                        help="report what would change and write nothing; exit 1 if anything would")
    parser.add_argument("--no-generate", action="store_true",
                        help="write the rows without regenerating the CSVs")
    args = parser.parse_args(argv)
    try:
        report = apply(args.workbook, check_only=args.check)
    except ReapplyError as error:
        print(f"Refused: {error}", file=sys.stderr)
        return 2
    for line in report:
        print(line, flush=True)
    if args.check:
        return 1 if any(line.endswith("(--check)") for line in report) else 0
    if args.no_generate:
        return 0
    result = subprocess.run([sys.executable, str(GENERATOR), "--workbook", str(args.workbook)],
                            cwd=REPO_ROOT)
    if result.returncode != 0:
        return result.returncode
    print("Now rebuild the assets: python tools/run_editor_python.py "
          "tools/generate_datatable_assets.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
