"""Turn the design into DataTable CSVs for Unreal.

    python tools/generate_datatables.py            # write the CSVs
    python tools/generate_datatables.py --check    # verify, change nothing

Output goes to game/Data/. Each CSV matches a USTRUCT declared in
game/Source/Cataclysm/Data/CataclysmDataRows.h, and an Unreal automation test
loads every CSV through its struct so a mismatch fails the build rather than
being discovered when something silently reads nothing.

THERE ARE TWO SOURCES, NOT ONE. Most tables come from the design workbook,
docs/All_Things_Cataclysm.xlsx, which is what a person edits. The two enemy
tables come instead from sim/cataclysm_sim/enemy_stats.py, because that is where
the enemy stat block is designed and where its self-checks live. `TABLES` holds
the workbook builders and `MODEL_TABLES` the Python-model ones; everything after
that treats them alike.

GENERATED FROM THE MODEL RATHER THAN COPIED OUT OF IT, deliberately. This project
already keeps one copy of a model by hand -- sim/cataclysm_sim/scoring.py against
the DungeonSimulator repository -- and CLAUDE.md records that it drifted silently
twice, which is why sim/verify_scoring_port.py had to be written. A second
hand-maintained copy would be the same mistake. Continuous integration runs
`--check`, so editing enemy_stats.py without regenerating fails the pull request.

WHY THIS IS NOT A LOOP OVER SHEETS

Not every workbook table comes from a sheet that is already a plain table with
one entity per row. Several sheets need reshaping, so each table has its own
handler:

  Enchantments      two independent tables side by side in one sheet, positives
                    in columns A-D and negatives in F-I
  Enemy Modifiers   a matrix, one column per Cataclysm, each cell holding
                    "Name: Description"
  Buffs, Debuffs    single column, no header row, each cell "Name: Description"
  DoTs              same shape as Buffs

Row names are derived and must be stable, because a DataTable row name is the key
everything else references. Renaming one silently breaks every reference to it.

THE ITEM BASES AND AFFIXES SHEETS HAVE A SECOND READER. `sim/cataclysm_sim/
affixes.py` holds the same pool, because that is where the design rules are
enforced and where tuning happens. The workbook is authoritative and is what a
person edits; `tools/tests/test_affix_sheets_match_the_model.py` compares the two
so they cannot drift apart silently, which a copy in this project has done before.
"""

from __future__ import annotations

import argparse
import csv
import functools
import io
import json
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKBOOK = REPO_ROOT / "docs" / "All_Things_Cataclysm.xlsx"
OUTPUT_DIR = REPO_ROOT / "game" / "Data"

#: The simulation package. It is not installed, so the enemy builders below reach
#: it by path, the same way tools/tests/test_brute_matches_the_model.py does.
SIM_ROOT = REPO_ROOT / "sim"

CATACLYSM_TYPES = ["Demonic", "Death", "War", "Pestilence", "Famine",
                   "Celestial", "Chaos", "Void", "Generic"]


class DataError(Exception):
    """A problem in the source data that must stop generation."""


# --------------------------------------------------------------------------
# helpers

def clean(value) -> str:
    """Normalise a cell to a trimmed string, collapsing internal whitespace."""
    if value is None:
        return ""
    return re.sub(r"\s+", " ", str(value)).strip()


def number(value, field: str, row: int) -> float:
    text = clean(value)
    if not text:
        raise DataError(f"row {row}: {field} is empty")
    try:
        return float(text)
    except ValueError as error:
        raise DataError(f"row {row}: {field} is {text!r}, not a number") from error


#: The first cell of the Crafting sheet's operations table, its header row. Issue #2830.
FORGE_OPERATIONS_HEADER = "Action"

#: The operations table's header, as `forge_operations` reads it: its five columns, the Base Gold column and the label of
#: the one cell every Base Gold follows. Issue #2830.
FORGE_OPERATIONS_COLUMNS = ["Action", "Core Materials Used", "CR Impact", "Base Days", "Consequence / Note",
                            "Base Gold", "Gold Per Craft Day"]


def forge_operations(book) -> list[dict]:
    """The Crafting sheet's operations table: what each forge operation consumes, its residue, days and gold.

    Issue #2830, slice G7 of the gems work. The owner, 2026-11-21: "I have costs set in the crafting excel sheet.
    Something like that should be implemented". The table begins at its own header row, whose first cell is
    "Action", and ends at the first empty row. Its Base Gold column follows ONE CELL, Gold Per Craft Day, beside the
    header: an operation of N base days costs that times N, at least once, and one whose days are "per tier" costs it
    once for each. Checked here, so a changed cell that leaves the column behind fails the generation rather than
    shipping two prices. The Combine row's Base Gold is its own figure, the lowest rung's price.

    WRITTEN AS NUMBERS IN THE SHEET, not formulas: this generator reads cached values only, and a formula written by
    a script has none until Excel recalculates it.
    """
    rows = list(book["Crafting"].iter_rows(values_only=True))
    start = next((at for at, raw in enumerate(rows) if raw and clean(raw[0]) == FORGE_OPERATIONS_HEADER), None)
    if start is None:
        raise DataError(f"Crafting has no operations table: no row begins {FORGE_OPERATIONS_HEADER!r}")
    header = [clean(cell) for cell in rows[start][:len(FORGE_OPERATIONS_COLUMNS)]]
    if header != FORGE_OPERATIONS_COLUMNS:
        raise DataError(f"Crafting row {start + 1}: the operations table's header reads {header}, not "
                        f"{FORGE_OPERATIONS_COLUMNS}")
    per_day = number(rows[start][len(FORGE_OPERATIONS_COLUMNS)], "Gold Per Craft Day", start + 1)

    out = []
    combines = 0
    for index, raw in enumerate(rows[start + 1:], start=start + 2):
        if not raw or not clean(raw[0]):
            break
        action = clean(raw[0])
        days = raw[3]
        gold = number(raw[5], "Base Gold", index)
        if action.startswith("Combine"):
            combines += 1
        else:
            expected = per_day * max(1, int(days)) if isinstance(days, (int, float)) else per_day
            if gold != expected:
                raise DataError(
                    f"Crafting row {index}: {action} has a Base Gold of {gold:g}, and the Gold Per Craft Day cell "
                    f"({per_day:g}) with its Base Days ({clean(days)!r}) makes it {expected:g}. Change the cell and "
                    f"the column together.")
        out.append({"Name": row_name("Operation", action), "Action": action, "Materials": clean(raw[1]),
                    "CRImpact": clean(raw[2]), "BaseDays": clean(days), "BaseGold": gold,
                    "Note": clean(raw[4])})
    if combines != 1:
        raise DataError(f"Crafting's operations table has {combines} Combine rows; the owner ruled one price for "
                        "stones and gems alike")
    return unique(out, "Crafting operations")


def row_name(*parts: str) -> str:
    """A stable, readable DataTable row name.

    Only letters, digits and underscores survive, because a row name is used as
    an FName and referenced from data.
    """
    joined = "_".join(p for p in parts if p)
    name = re.sub(r"[^A-Za-z0-9]+", "_", joined).strip("_")
    if not name:
        raise DataError(f"could not build a row name from {parts!r}")
    return name


def split_named(text: str) -> tuple[str, str]:
    """Split a "Name: Description" cell. Returns ("", text) if there is no colon."""
    if ":" in text:
        name, _, description = text.partition(":")
        return name.strip(), description.strip()
    return "", text


def unique(rows: list[dict], table: str) -> list[dict]:
    """Make row names unique by suffixing duplicates, and report how many."""
    seen: dict[str, int] = {}
    for row in rows:
        base = row["Name"]
        if base in seen:
            seen[base] += 1
            row["Name"] = f"{base}_{seen[base]}"
        else:
            seen[base] = 0
    return rows


# --------------------------------------------------------------------------
# per-sheet handlers

def dungeon_modifiers(book) -> list[dict]:
    out = []
    for index, raw in enumerate(book["Dungeon Modifiers"].iter_rows(values_only=True), 1):
        if index == 1 or not raw or not clean(raw[0]):
            continue
        cataclysm, name, weight, description = (clean(raw[0]), clean(raw[1]),
                                                raw[2], clean(raw[3]))
        if not name:
            raise DataError(f"Dungeon Modifiers row {index}: modifier name is empty")
        if cataclysm not in CATACLYSM_TYPES:
            raise DataError(f"Dungeon Modifiers row {index}: "
                            f"{cataclysm!r} is not a Cataclysm type")
        out.append({"Name": row_name(cataclysm, name), "CataclysmType": cataclysm,
                    "ModifierName": name,
                    "Weight": number(weight, "Weight", index),
                    "Description": description})
    return unique(out, "Dungeon Modifiers")


#: Riders any shape may carry, and what each one means.
#:
#: Eight of the sixteen designed Demonic skills leave a burning patch of ground
#: behind whatever else they do, so the ground zone is a RIDER on a shape rather
#: than a shape of its own. Issue #37's own table hints at this: it lists
#: "persistent ground zone" against "used by most of the above" instead of
#: naming skills, where every other entry names skills.
SHAPE_RIDERS = {
    "Burn": "1 if the skill sets what it hits alight, 0 or absent if not",

    # WHOM A BUFF BURNS, WHICH `Burn` ABOVE DOES NOT SAY. A self buff
    # stating `Burn` may mean "the pinned enemies in my radius, once a
    # second" (the Spear's Held Fast) or "whatever strikes me in melee"
    # (the Greataxe's Burning Wrath). One key cannot mean both, so the
    # second is its own. Added 2026-09-02.
    "BurnsAttackers": "1 if a running buff sets alight whatever strikes its "
                      "holder in melee, 0 or absent if not",
    "GroundRadius": "metres of burning ground left behind, 0 for none",
    "GroundDuration": "seconds that ground burns",
    "GroundPercent": "percent of the skill's damage that ground deals per "
                     "second. Decided 2026-08-14 on issue #361: standing in it "
                     "for its whole GroundDuration costs one hit of the skill "
                     "that left it, so this is 100 divided by GroundDuration. "
                     "It keeps burning ground area denial rather than a second "
                     "damage source, and stops a longer patch being "
                     "automatically a bigger one",
    "GroundHitsAllies": "1 if that ground burns everything standing in it "
                        "whatever side it is on, including whatever left it. "
                        "Absent or 0 means it burns only the caster's enemies, "
                        "which is what every player skill wants. The Hellhound's "
                        "fire trail is the only thing that sets it",
    "StunSeconds": "how long the target is stunned, if the skill stuns. The "
                   "anti-stun-lock rule in section VI still applies: it is the "
                   "duration, not a promise that a stun lands",
    "Knockback": "metres the skill pushes what it hits away from the caster, 0 "
                 "or absent for none. A RIDER RATHER THAN A STRIKE PARAMETER, "
                 "decided 2026-08-15 on issue #626: displacement is not specific "
                 "to one kind of skill. A strike, a leap, a charge and an enemy "
                 "slam can all shove, and while this was a parameter of Strike "
                 "alone, Shockwave Leap knocked back in its prose and could not "
                 "say so in its data. That is the same argument that made a "
                 "burning patch of ground a rider",
    "FinalHitPercent": "percent of weapon damage a closing hit deals, if any",
    "HealthCostPercent": "percent of current health one use costs, if any",
    "Effect": "the named status effect this applies, from the Buffs, Debuffs "
              "or DoTs sheet. More than one may be named, comma separated",

    # --- Added 2026-09-01 with the Demonic verb rewrite ---------------------
    #
    # THE BUCKET IS IN THE NAME, NOT IN A VALUE. `MoreDamagePer` multiplies
    # separately and `IncreasedDamagePer` joins the additive sum, which is the
    # distinction section VI draws and the single most important thing about a
    # damage number in this project. A shared key with a bucket beside it would
    # have made the two one typo apart.
    "MoreDamagePer": "percentage points of MORE damage -- the multiplicative "
                     "bucket -- per unit of ScalingSource",
    "IncreasedDamagePer": "percentage points of INCREASED damage -- the "
                          "additive bucket -- per unit of ScalingSource",

    # A FLAT MULTIPLIER ON A CONDITION RATHER THAN A RATE PER COUNT, which is
    # what separates this from the two keys above. Those need a ScalingSource
    # to say what they count; the condition here is where the attacker stood.
    # Added 2026-09-02 for the Dagger's Emberpierce, whose "40% more damage
    # from behind" had no parameter at all and so did nothing.
    "MoreDamageFromBehind": "percentage points of MORE damage -- the "
                            "multiplicative bucket -- on a blow landed from "
                            "behind the target",
    "DurationPer": "seconds added to the skill's own duration per unit of "
                   "ScalingSource",
    "ScalingSource": "what the three keys above count, one of SCALING_SOURCES",

    # Displacement that a distance cannot say. `Knockback` above stays exactly
    # what issue #626 made it -- metres pushed away from the caster -- because
    # that decision generalised it on purpose and three enemy abilities write
    # it. This rider carries the five verbs a distance does not express: put on
    # the floor, thrown upward, hauled inward, dragged along, or nailed in
    # place. A skill that simply shoves still writes Knockback.
    "ForcedMovement": "what the skill does to a target beyond pushing it, one "
                      "or more of FORCED_MOVEMENTS",
    "ForcedMovementDistance": "metres a pull or drag carries the target",
    "ForcedMovementDuration": "seconds the target is held where it was put",

    # Geometry that outlives the hit. Distinct from the burning ground rider
    # above: that is a damage patch, this changes where a fight can happen.
    "Terrain": "persistent geometry the skill leaves, one of TERRAIN_KINDS",
    "TerrainSize": "metres -- radius for a pit, fissure or thicket, length for "
                   "a wall",
    "TerrainDuration": "seconds the geometry persists",

    # A skill that does not fire, or does not pay out, without its condition.
    "Requires": "a condition the skill needs, one or more of REQUIREMENTS",
    "Immune": "what the caster cannot be subjected to while the skill runs, "
              "one or more of IMMUNITIES. Not immunity to DAMAGE, which is a "
              "different axis and is issue #1162",

    # NOT `Duration`, DELIBERATELY. Anathema's ten seconds is how long its curse
    # sits on an enemy and Butcher's Bill's ten seconds is how long the skill
    # itself runs. They were the same word, and a reader could not tell which
    # one a row meant.
    "EffectDuration": "seconds an applied Effect lasts, never a channel length",
    "EffectMagnitude": "size of the applied Effect, in whatever unit that "
                       "effect is measured in",

    # THE ONE THING AN AURA GIVES RATHER THAN TAKES. Added 2026-09-02 for
    # Conflagration's "allies within it deal 8% increased fire damage", which
    # had no key at all, so half the row lived only in its prose. Issue #1182.
    #
    # `increased` IS THE ADDITIVE BUCKET AND THE NAME SAYS SO, the same way
    # `MoreDamagePer` and `IncreasedDamagePer` below carry their bucket in their
    # names. It joins the sum every gear affix and passive node joins.
    #
    # NOT `IncreasedDamagePer`, WHICH IS A DIFFERENT NUMBER. That is per unit of
    # a ScalingSource and sizes one blow. This is a flat grant to somebody else
    # for as long as they stand inside.
    "AllyIncreasedDamage": "percentage points of increased damage an aura "
                           "grants allies standing in it, to damage of every "
                           "element (ruled 2026-10-25, Q-P2)",

    # THE DAMAGE-TYPE AURAS MADE DIFFERENT FROM ONE ANOTHER, the owner's rulings of 2026-11-25: "make it 10% more and
    # I'm good with it" (Death), "Go with A for both" (Demonic, War), and "I think we should avoid making all of the
    # aura's kinda samey".
    "AllyMoreDamage": "percentage points of MORE damage, a separate multiplier, an aura grants its caster and every "
                      "ally standing in it, minions included (Death's Pall of Ending)",
    "BurnPassesOnDeath": "metres: a burning enemy that dies inside an aura passes its strongest burn, with the seconds "
                         "it had left, to the nearest other enemy within this many metres, as the caster's burn "
                         "(Demonic's Conflagration)",
    "EnemyArmourRemoved": "percent of armour an aura removes from every enemy inside it, held two pulses (War)",
    "EnemyMovementSpeedReduced": "percent slower every enemy inside an aura moves, movement only, held two pulses "
                                 "(War)",
    "AllyMovementSpeed": "percent faster an aura's caster and every ally inside it move, held two pulses (War)",
    "AllyImmune": "what every ally inside an aura cannot be subjected to, granted two pulses at a time, in Immune's "
                  "words (War: Slow)",

    # THE OTHER HALF OF THE SAME EVENT `ScalingSource=HitTaken` READS. Added
    # 2026-09-02 for Living Pyre, "returns health equal to 25% of the damage
    # that hit dealt", which had no key. Of what reached health, not of what
    # was sent. It is NOT leech: leech is what a hit gives back to whoever
    # landed it, paid out over three seconds. Issue #1162.
    "HealthFromHitTaken": "percent of a blow taken that is returned to the "
                          "caster as health while the skill runs",

    # THE OTHER WAY A SKILL MAY USE A BLOW TAKEN. Added 2026-09-02 for Martyr's
    # Ember, which holds it as damage to give back rather than turning it into
    # health at once. Three keys because the row states three numbers -- "40% of
    # all damage you take is stored", "capped at 200% weapon damage", and a
    # spend the row does NOT state, whose value is a judgement recorded in
    # docs/DECISIONS.md. Issue #1162.
    "StoresFromHitTaken": "percent of a blow taken that is put into the "
                          "skill's store instead of being paid back",
    "StoreCapPercent": "ceiling on that store, as percent of weapon damage",
    "StoreSpentPerHit": "percent of weapon damage one landed blow takes out "
                        "of the store and adds to what it hit. Once per enemy "
                        "hit, not once per use",

    # THE ONE THING A PATCH OF BURNING GROUND GIVES THE PERSON WHO LEFT IT.
    # Added 2026-09-02 for Blood Pyre, "standing in your own pyre does you no
    # harm and doubles your health regeneration", whose second half had no key.
    # The first half needed none: a ground zone already burns only its owner's
    # enemies. Issue #1162.
    "OwnGroundRegenPercent": "what the caster's health regeneration becomes, "
                             "as a percent of normal, while it stands in the "
                             "burning ground this skill left",

    # WHAT A HELD SWING TAKES AWAY FROM ITS OWN CASTER BEYOND MOVING. Added
    # 2026-09-02 for The Whole Weight, "you cannot move, act or be healed",
    # whose second and third words had nothing behind them. Moving is refused
    # for every held swing and needs no key, because both charged rows say so.
    # Issue #1162.
    "HoldForbids": "what the caster may not do while this skill is held, one "
                   "or more of HOLD_FORBIDS",

    "OnDeath": "what happens when an affected enemy dies, one of ON_DEATHS",
    "OnDeathRange": "metres the on-death effect reaches for its next target",

    "ConsumeBurn": "1 if the skill spends the target's burn rather than only "
                   "applying it. The Sword's verb",
    "ConsumeRadius": "metres of whatever consumption produces",

    # SPREADING FIRE WITHOUT CONSUMING IT, which is the opposite of the two
    # keys directly above. Consuming takes the burn OUT of a target and
    # spends it; this leaves it burning and lights its neighbours as well.
    # The Wand's Hex of Cinders is the only row that states it, and issue
    # #1146 records why it is not written with GroundRadius instead.
    "SpreadWhen": "what a TARGET must already carry for the skill to spread "
                  "fire from it, one of SPREAD_CONDITIONS. Not a condition "
                  "on the cast, which is Requires",
    "SpreadRadius": "metres the fire spreads from a target that met "
                    "SpreadWhen",

    "MaxDamagePercent": "ceiling on a skill whose damage scales, as percent of "
                        "weapon damage",
    "MinDamagePercent": "floor for a charged skill released immediately, as "
                        "percent of weapon damage",

    "ChargeTime": "seconds of hold before a full release",
    "ChargeBreaksOn": "what cancels a hold and loses the skill, one or more of "
                      "CHARGE_BREAKS. None means nothing can",

    "RefundsCooldown": "which cooldown the skill returns, one of "
                       "REFUND_TARGETS. What triggers it is the row's Trigger "
                       "tag",
    "Untargetable": "1 if the caster cannot be hit while the skill runs",
    "PassesThroughEnemies": "1 if a walked charge passes through enemy bodies "
                            "for its walk, as Hollow Step's "
                            "movement_passes_through_enemies makes it",

    # --- Added 2026-11-17 for the Corrupted Stalker's kits, issue #2580 -----
    #
    # [N1], RULED BY THE OWNER THAT DAY. `Effect` cannot say it: the engine's
    # `NamedEffectTags` drops every damage over time `Effect` names, because a
    # tag granted with no per-tick amount behind it is a target bleeding for
    # nothing. This key prices one, through `UCataclysmAilments::Apply`.
    "Ailment": "the ailment a landed blow applies outright, at magnitude one, "
               "one of SKILL_ROW_AILMENTS. Burn is `Burn=1` and not this",
    # [N3]. NOT `MinDamagePercent` AND `MaxDamagePercent`, which already mean
    # a charged swing's floor and a scaling ceiling. A row stating a roll
    # leaves its Damage Percent cell blank; `weapon_skills` refuses both.
    "DamageRollMin": "the least percent of weapon damage a use may draw; "
                     "drawn uniformly per use up to DamageRollMax",
    "DamageRollMax": "the most percent of weapon damage a use may draw",
    # [N4]. One of the named effects per application rather than all of them.
    "EffectPick": "how a row naming several effects lays them, one of "
                  "EFFECT_PICKS. `Random` lays one, drawn on each application",
}

#: Closed value sets for the text riders above.
#:
#: CLOSED FOR THE SAME REASON THE PARAMETER LIST IS. A misspelled value that
#: parsed would be a skill that runs and quietly does nothing, which is the
#: failure `parse_shape_params` exists to prevent one level up.
#:
#: `Fervour` JOINED ON 2026-11-24, issue #2885 slice DS3: the Death draft's N7,
#: the Fervour held when the blow lands or while the buff runs. The Greataxe's
#: Grim Swath ("1% more for every 2 Fervour you hold") on a Strike, and the
#: Sword's Cold Blood ("1% more damage for every 4 Fervour you hold") on a self
#: buff; `check_death_skill_keys` refuses it on any other shape, where nothing
#: reads it.
#:
#: `FervourSpent` JOINED THE SAME DAY WITH SLICE DS4: the Death draft's N13,
#: the Fervour a use spent when its row states `SpendsAllFervour=1` -- the
#: Wand's Requiem, "1% more for every Fervour spent".
#: `check_a_spending_or_cashing_strike` refuses it without that key.
SCALING_SOURCES = frozenset({
    "Kill", "Burning", "Second", "Meter", "HitTaken", "Consume", "Consumed",
    "Bounce", "Pierced", "Pinned", "HealthMissing", "Fervour", "FervourSpent",
})

#: The shapes that read `ScalingSource=Fervour`: a Strike's blow, through
#: `UCataclysmSkillTemplate::ScalingUnits`, and a self buff's bonus, through
#: `UCataclysmSelfBuffSkill::ScalingCount`. Issue #2885, N7.
FERVOUR_SCALING_SHAPES = frozenset({"Strike", "SelfBuff"})
FORCED_MOVEMENTS = frozenset({"Knockdown", "Launch", "Pull", "Drag", "Pin"})
TERRAIN_KINDS = frozenset({"Pit", "Wall", "Fissure", "Thicket"})
REQUIREMENTS = frozenset({"Burning", "Target", "Stationary", "RearHit"})
#: `Rise` JOINED ON 2026-11-24, issue #2885 slice DS3: the Death draft's N5,
#: the Greataxe's Death Rattle, "any that dies while it rattles rises at once
#: as a husk, the same husk Shallow Graves raises". A Debuff's alone, beside
#: `RisesAs=<kind>`, which `validate_risen_kinds` holds to a Summon row of the
#: same damage type raising that kind.
#:
#: `Burst` JOINED THE SAME DAY WITH SLICE DS5: the Death draft's N14, the
#: Fist's Gravetouch, "if it dies while the wound still bleeds, it bursts,
#: dealing 10% of its maximum health to every enemy within 4 meters". Beside
#: the row's `Ailment` (the wound), `BurstPercentOfMaximumHealth` and
#: `OnDeathRange`, which `check_a_bursting_wound_a_paid_hit_and_an_escape`
#: asks for.
ON_DEATHS = frozenset({"Leap", "SpreadDebuff", "Release", "Rise", "Burst"})

#: How a `RisesAs` value is written: one Minion Types row's name.
RISEN_KIND = re.compile(r"^[A-Za-z][A-Za-z0-9]*$")

#: What a skill may make its caster immune to while it runs.
#:
#: THE DESIGN'S OWN WORDS, from the table in section VI of
#: `docs/Cataclysm_GDD_v2.md` listing which effects the anti-stun-lock rule
#: covers: Stun, Knockdown, Slow, Displacement and Madness. Pin is added
#: because it is an effect a row could reasonably refuse and the engine already
#: applies it, and CrowdControl names all six at once -- which is what "immunity
#: to all crowd control" says and what saves a row from listing six things to
#: mean one.
#:
#: SEVEN ROWS ACROSS FOUR WEAPONS STATE ONE. Section VI names five of them
#: outright -- Living Pyre, Unstoppable Force, Forge Stance, Bull Rush and
#: Cinder Rush -- and the Greatsword's Unbroken and Inexorable are the other
#: two. None had a parameter until 2026-09-02.
IMMUNITIES = frozenset({
    "Stun", "Knockdown", "Slow", "Displacement", "Pin", "Madness",
    "CrowdControl",
})
CHARGE_BREAKS = frozenset({"Stagger", "Death", "Movement", "None"})

#: What a skill may forbid its own caster while it is being held.
#:
#: ONE ROW STATES ANY OF THEM. The Greatsword's The Whole Weight: "you cannot
#: move, act or be healed." Moving is refused for EVERY held swing, because both
#: charged rows say so, and needs no key; these are the two extra restrictions
#: that only this row asks for. Backswing states none and says only "you cannot
#: move while holding". Issue #1162.
HOLD_FORBIDS = frozenset({"Acting", "Healing"})
REFUND_TARGETS = frozenset({"Self", "Movement"})
TARGET_MODES = frozenset({"All", "Nearest", "Furthest"})

#: What a target may already carry for a skill to spread fire from it.
#:
#: ONE VALUE, because one row states the key. It is a closed set rather than
#: free text for the reason every other rider here is: a misspelling would
#: otherwise reach the engine, fail to match any branch, and spread nothing
#: while the row read as though it worked.
SPREAD_CONDITIONS = frozenset({"Burning"})

#: The ailments a skill row's `Ailment=` may name. Issue #2580, [N1], ruled by
#: the owner on 2026-11-17: "Build the new skill-row key
#: `Ailment=<Poison|Disease|Necrosis|VoidSplinter>`". Written without the
#: space `UCataclysmAilments` spells "Void Splinter" with, because a cell value
#: is one word; `UCataclysmAilments::KindNamedInASkillRow` compares without
#: spaces.
#:
#: BURN IS LEFT OUT ON PURPOSE, a judgement of 2026-11-17 recorded in
#: docs/DECISIONS.md: `Burn=1` already applies it through `ApplyBurn`, which
#: carries burn's own rules, and two keys for one ailment would let a row state
#: it twice.
#:
#: BLEED JOINED ON 2026-11-24, issue #2885, the Death draft's N16: the owner
#: accepted the Death and War plan's Q22 that day ("Your recommendations work
#: for all of those death/war questions"), which widens the owner's four by
#: Bleed for the Fist's Gravetouch and Severing Rush. `UCataclysmAilments`
#: already lays one by name (`KindNamedInASkillRow`), with bleed's movement
#: gate (issue #918), so this set is the whole of the change.
SKILL_ROW_AILMENTS = frozenset({"Poison", "Disease", "Necrosis", "VoidSplinter",
                                "Bleed"})

#: How a row naming several effects lays them. Issue #2580, [N4]. Absent means
#: every one, which is what Anathema's `Effect=Shred, Madness` does.
EFFECT_PICKS = frozenset({"Random"})

#: The rider text keys that take ONE value and never a comma-separated list.
#: `Ailment=Poison, Necrosis` would otherwise pass the closed-set check one name
#: at a time and the engine would read no ailment at all.
SINGLE_VALUED_TEXT_PARAMS = frozenset({"Ailment", "EffectPick"})

#: The name `Effect=` writes on a self buff that draws one buff from the
#: game's own random-buff pool, `UCataclysmAbilitySystemComponent::
#: RandomBuffTable`. Issue #2580, [N5], the Chaos Whip's Loaded Dice. It names
#: no row of the Status Effects sheet, so `validate_skill_effects` passes it by
#: name; `UCataclysmSkillTemplate::RandomBuffEffect` holds the same text.
RANDOM_BUFF_EFFECT = "RandomBuff"

#: The shapes whose blows are dealt by something the row puts in the world --
#: a minion, a gadget -- rather than through the skill's own `HitTargets`, so a
#: stated ailment would reach nothing. Refused rather than left to do nothing.
SHAPES_WHOSE_BLOWS_ARE_NOT_THEIR_OWN = frozenset({"Summon", "Deployable"})

#: Which closed set each text rider draws from. A rider absent from this map is
#: a number.
TEXT_PARAM_VALUES = {
    "ScalingSource": SCALING_SOURCES,
    "ForcedMovement": FORCED_MOVEMENTS,
    "Terrain": TERRAIN_KINDS,
    "SpreadWhen": SPREAD_CONDITIONS,
    "Requires": REQUIREMENTS,
    "Immune": IMMUNITIES,
    "AllyImmune": IMMUNITIES,
    "HoldForbids": HOLD_FORBIDS,
    "OnDeath": ON_DEATHS,
    "ChargeBreaksOn": CHARGE_BREAKS,
    "RefundsCooldown": REFUND_TARGETS,
    "TargetMode": TARGET_MODES,
    "Ailment": SKILL_ROW_AILMENTS,
    "EffectPick": EFFECT_PICKS,
}

#: The parameters whose value is a name rather than a number.
#:
#: `Minions` is a comma-separated list of `Type:Count` pairs naming what a skill
#: produces, added with issue #338. It is a list rather than one name because
#: Iron Fortress deploys two ballistae AND three spike traps, which no single
#: key and value can say. Every type it names must exist in the Minion Types
#: sheet, and `validate_minion_references` refuses one that does not.
#:
#: `RisesAs` JOINED ON 2026-11-24, issue #2885 slice DS3, N5: the kind that
#: rises from a creature dying under a curse, a Minion Types row's name, which
#: `validate_risen_kinds` checks against the sheet as `Minions` is checked.
TEXT_PARAMS = frozenset({"Mode", "Effect", "Minions", "RisesAs"}
                        | set(TEXT_PARAM_VALUES))

#: How a `Minions` entry is written: `Imp:1`, or `Ballista:2, SpikeTrap:3`.
MINION_ENTRY = re.compile(r"^\s*([A-Za-z][A-Za-z0-9]*)\s*:\s*(\d+)\s*$")


def parse_minions(text: str, where: str) -> dict[str, int]:
    """Read a `Minions` value into {type name: how many}."""
    out: dict[str, int] = {}
    for piece in text.split(","):
        piece = piece.strip()
        if not piece:
            continue
        match = MINION_ENTRY.match(piece)
        if not match:
            raise DataError(
                f"{where}: minion entry {piece!r} is not Type:Count")
        name, count = match.group(1), int(match.group(2))
        if name in out:
            raise DataError(f"{where}: {name} is listed twice")
        if count < 1:
            raise DataError(
                f"{where}: {name} is produced {count} times, so naming it says "
                "nothing")
        out[name] = count
    if not out:
        raise DataError(f"{where}: Minions is set and names nothing")
    return out

#: The closed list of shapes, and which parameters each one reads.
#:
#: A SHAPE NAMES WHICH TEMPLATE RUNS. It is deliberately NOT read off the Tags
#: column, even though that column already carries Type.Projectile,
#: Type.AOE.PointBlank and the rest. Two reasons, and the second is the one that
#: would have caused a real bug:
#:
#:   The tags do not decide it. Molten Cleave carries Type.AOE.PointBlank,
#:   Type.Strike AND Type.AOE.Persistent, and nothing says which is the primary
#:   behaviour. Infernal Plunge is a leap and carries no tag saying so.
#:
#:   The tags already have a job. UCataclysmStatPipeline::ModifierApplies scopes
#:   every gear increase by the tags of the skill in hand. Dispatching on them
#:   too would mean adding a tag to make a skill's shape work silently changed
#:   which gear modifiers apply to it.
#:
#: Path of Exile draws the same line: its gems.json carries `types` (the internal
#: list, whose stated purpose is deciding which support gems may support a skill)
#: separately from the behaviour the skill's own ActiveSkills.dat id names.
#: A PROJECTILE STATES `Speed` OR `Flight`, NOT BOTH. `Speed` is centimetres per
#: second and describes something travelling flat, which is what all 398 player
#: projectile rows use. `Flight` is seconds in the air and describes a LOB
#: following real projectile motion onto the point it was aimed at, whose speed
#: and arc height both fall out of the time. `SHAPE_PARAMS` in
#: `sim/cataclysm_sim/enemy_abilities.py` is the same table and says the same.
#: Issue #465.
SHAPE_PARAMS = {
    "Strike": {"Radius", "Angle", "MaxTargets", "Duration", "Interval",
               # Buried Fire leaves the greatsword in the ground and the player
               # fights bare-handed until it is pulled free. Only it states this.
               "DisarmsUntilRecalled",
               # BOTH JOINED ON 2026-11-24 WITH THE DEATH SKILLS, issue #2885,
               # for the Greatsword's Gravebell: N3, `Detonates`, explodes every
               # minion of the caster's inside the swing, and N4, `CommandStrike`,
               # orders everything else it commands onto the first enemy the
               # swing struck, as Compel orders them onto what its bolt hit.
               "Detonates", "CommandStrike",
               # SLICE DS3, 2026-11-24, issue #2885: N6, `KillFervour`, extra
               # Fervour for each enemy the skill's own blow kills (Grim Swath,
               # Last Toll). On the four shapes whose blows are their own, and
               # `check_death_skill_keys` says what a Debuff's means.
               "KillFervour",
               # SLICE DS4, 2026-11-24, issue #2885: N13, `SpendsAllFervour`,
               # a use that spends every Fervour held (the Wand's Requiem),
               # and N15, `CashInRadius`, the caster's own Poison, Bleed and
               # Necrosis around it deal all they had left (the Fist's
               # Quietus). N9 needs no key: `RefundsCooldown` is a rider, and
               # a strike now reads it.
               "SpendsAllFervour", "CashInRadius",
               # SLICE DS5, 2026-11-24, issue #2885: N14, the share of the
               # dying creature's maximum health a wound bursts for, beside
               # `OnDeath=Burst` (the Fist's Gravetouch).
               "BurstPercentOfMaximumHealth"},
    # A PROJECTILE MAY NOW REPEAT. Butcher's Bill throws thirty axes over ten
    # seconds, which needs the same Count, Duration and Interval a Summon has
    # always had; before this it could only be written as prose. `TargetMode`
    # says who a repeating projectile picks, and is the same word and the same
    # values as the column of that name in the Minion Types sheet.
    "Projectile": {"Range", "Radius", "Pierce", "Returns", "Speed", "Arc",
                   "Count", "Duration", "Interval", "TargetMode",
                   "ScalesWithAttackSpeed", "Bounces", "SpreadCurses",
                   "CommandStrike",
                   "TetherTargets", "TetherLength", "TetherDuration",
                   # N3, issue #2885, for the Staff's Marrow Lance: the
                   # caster's minions on the line it flew explode.
                   "Detonates",
                   # N6, slice DS3: Restless Spirits' kills, and Hungering
                   # Soul's hosts, whose buried soul carries it on.
                   "KillFervour",
                   # N19, slice DS5: the Spear's Soul Lance, Fervour for each
                   # enemy a flying shot lands on, to a cap a cast.
                   "HitFervour", "HitFervourPerCast",
                   # The Crossbow draft's C1, slice DS5: the Crossbow's Coffin
                   # Nail, a nail's critical strike chance and the nails' ceiling.
                   "CritChanceAgainst", "CritChanceAgainstMax"},
    # `Interval` JOINED ON 2026-09-02 WITH THE SPEAR'S HELD FAST, whose
    # sentence has a per-second half: "any pinned enemy within 12 meters is
    # set alight again each second it is held". Until then a self buff was
    # the one lasting shape that could not repeat, so that half of the row
    # could not be written down at all. Strike, Projectile, Movement, Summon,
    # Deployable and Aura all already had it.
    #
    # `HoldsFervour` JOINED ON 2026-11-24, issue #2885 slice DS3: the Death
    # draft's N8, the Sword's Cold Blood, "while it lasts your Fervour does not
    # decay". `UCataclysmFervour::DecayStep` asks whether a running buff holds it.
    #
    # `MeleeReachMetres` AND `EveryBlow` JOINED THE SAME DAY WITH SLICE DS4:
    # the Death draft's N21, flat metres a running buff adds to its holder's
    # melee reach (the Spear's Grim Reach), and N17, a buff whose `Ailment`,
    # `Effect` and `ForcedMovement` are laid by every blow its holder lands
    # (Hemlock, Death Grip, Grim Reach). `UCataclysmSelfBuffSkill::
    # NoteBlowLanded` lays them; `check_a_bursting_rift_and_a_buff_on_every_blow`
    # refuses either where it would do nothing.
    #
    # `StrikesOnCritical` JOINED WITH SLICE DS5: the Crossbow draft's C2, a buff
    # whose holder's critical strikes each strike again for the row's Damage
    # Percent (the Crossbow's Carrion Crows); `check_a_buff_striking_on_a_
    # critical_states_its_damage` asks the row for that figure.
    "SelfBuff": {"Duration", "Interval", "Radius", "RangeIncrease",
                 "HoldsFervour", "MeleeReachMetres", "EveryBlow",
                 "StrikesOnCritical"},
    # Inexorable walks for three seconds and Everywhere at Once flickers for
    # four, so a movement can now last and repeat. `RearHits` says every blow it
    # lands counts as struck from behind.
    #
    # `MaxTargets` WAS MISSING AND THE ENGINE HAS ALWAYS READ IT. All three
    # movement modes that hit anything hand it straight to the targeting search:
    # a charge caps what it catches along the line, and a leap and a blink cap
    # what they catch at the ends. A row stating it would therefore have worked,
    # and this table refused to let one say so.
    #
    # WHAT MADE IT VISIBLE is the Spear's Nail Down, which impales "the first
    # enemy you reach" where the Whip's Reel takes "every enemy the line
    # crosses". The two are both charges and differ in nothing else, so without
    # this there is no way to write the difference down at all.
    #
    # `Detonates` JOINED ON 2026-11-24, issue #2885, N3: Barrow Leap, Open
    # Grave and Grave Return explode the caster's minions where they arrive.
    # `check_death_skill_keys` refuses it on a flicker and a walked charge,
    # whose blows are struck elsewhere and do not detonate.
    #
    # `KillFervour` JOINED ON 2026-11-24, slice DS3, N6: Pale Rider's kills, and
    # Danse Macabre's deaths during the dance. `MaxDuration` JOINED THE SAME
    # DAY, N10: the ceiling on a flicker its deaths lengthen by `DurationPer`
    # ("up to 8 seconds in all"); `check_death_skill_keys` says where.
    "Movement": {"Mode", "Range", "Radius", "MaxTargets", "Duration", "Interval",
                 "RearHits", "Detonates", "KillFervour", "MaxDuration"},
    # Subjugate takes ONE enemy, which is what MaxTargets says everywhere else.
    #
    # `Taunts` JOINED ON 2026-11-24 WITH THE DEATH SKILLS, issue #2885: the
    # Crossbow draft's C3, approved by the owner that day for the Greataxe's
    # Shallow Graves, whose husks "turn the enemies around them on themselves".
    # Each minion the row raises taunts every enemy within the row's `Radius` of
    # it as it rises and once a second after (`ACataclysmMinion::BeginTaunting`).
    # A threat figure could not say it: only Behind the Veil reads one.
    #
    # `Permanent` AND `MinionsLayEffect` JOINED ON 2026-11-24, issue #2885:
    # the Death draft's N1, a raise that stands until it is destroyed (Barrow
    # Guard, Exhume, Bone Choir), and N11, minions that lay the row's `Effect`
    # with each blow (Bone Choir's mages). N2 needs no key: a Summon row that
    # states a Damage Percent strikes as it raises, and lays its `Effect` with
    # that blow (`check_a_summon_effect_has_a_carrier`).
    #
    # `CollapseBursts` JOINED WITH SLICE DS4: the Death draft's N12, what a rift
    # raised bursts as the rift closes rather than being destroyed (the Staff's
    # Grave Mouth); refused on a summon that is not a rift, which never closes.
    "Summon": {"Range", "Radius", "Count", "MaxActive", "Duration", "Interval",
               "Minions", "MaxTargets", "Possess", "FervourReserve",
               "HealthThresholdPercent", "Taunts", "Permanent",
               "MinionsLayEffect", "CollapseBursts"},
    # AN EIGHTH SHAPE, added with issue #338. A summon spawns things that walk to
    # the enemy; a deployable places things that stay where they are put. The
    # split is behavioural and the data already carried it as a tag -- Bolt
    # Turret, Ballista and Iron Fortress all have Type.Deployable and the three
    # summons have Type.Summon -- but the Shape column was empty for all three,
    # so every number they state lived only in their prose description where no
    # code could read it.
    #
    # The name matches the existing tag rather than inventing a new word.
    # FERVOURRESERVE JOINED ON 2026-09-08. The project owner ruled that every
    # minion and gadget skill reserves Fervour, not only the Staff Ultimate
    # that takes thralls. Three of the four rows that gained the key are
    # Deployable -- Bolt Turret, Ballista and Iron Fortress -- and the shape
    # did not allow it, so the generator refused them. Summon already allowed
    # it because Subjugate has carried it since 2026-09-01.
    #
    # `Taunts` JOINED ON 2026-11-24, as on Summon above: a placed machine
    # taunts every enemy within the row's `Radius` of it each `Interval`, or
    # each second when the row states none. The Death Crossbow's Effigy is the
    # row it was drafted for (issue #2885); it waits on the Siphon (C4).
    "Deployable": {"Range", "Radius", "Count", "MaxActive", "Duration",
                   "Interval", "Minions", "HealthPercent", "FervourReserve",
                   "Taunts"},
    "Aura": {"Radius", "Duration", "Interval"},
    # `KillFervour` JOINED ON 2026-11-24, slice DS3, N6: the Sword's Death
    # Warrant, "if it dies while the warrant holds, you gain 10 Fervour". A
    # curse pays for a death it marks, so `check_death_skill_keys` asks for
    # `OnDeath=SpreadDebuff` beside it. `RisesAs` JOINED THE SAME DAY, N5: the
    # kind that rises from a creature dying under the curse, beside
    # `OnDeath=Rise` (Death Rattle's husk).
    "Debuff": {"Range", "Radius", "MaxTargets", "Duration",
               "SpreadWhen", "SpreadRadius", "KillFervour", "RisesAs"},
}

#: The movement modes, and what each one is.
#:
#: THREE WERE ADDED 2026-09-01 with the Demonic verb rewrite, because three
#: weapons' verbs are movements nothing existing could describe. `Swap`
#: exchanges the caster's position with a minion's, which is the Staff's
#: Vesselstep and the only movement in the game that costs something the player
#: owns. `Recall` returns to a mark left earlier rather than departing, which is
#: what separates the Dagger's Echo from its Ashwalk. `Flicker` repeats,
#: arriving at one enemy after another, and is the only movement that is also an
#: ultimate.
MOVEMENT_MODES = {"Leap", "Charge", "Blink", "Swap", "Recall", "Flicker"}

#: The two shapes a basic attack may take. `docs/Cataclysm_GDD_v2.md` says the
#: basic attack is the weapon's own swing -- a Strike for a melee weapon, a
#: Projectile for a ranged one -- so any other shape in that column is a mistyped
#: cell rather than a design. Issue #524.
BASIC_ATTACK_SHAPES = frozenset({"Strike", "Projectile"})


def parse_shape_params(text: str, shape: str, where: str) -> dict[str, str]:
    """Read a `Key=Value; Key=Value` cell, refusing anything the shape cannot use.

    REFUSING IS THE POINT. A parameter this returned silently as zero looks
    exactly like a parameter nobody wrote, which is how a cooldown of zero went
    unnoticed across 77 skills in issue #155. A radius of zero hits nothing, so
    a misspelled `Radiuss` would produce a skill that runs and does nothing.
    """
    params: dict[str, str] = {}
    if not text:
        return params

    allowed = SHAPE_PARAMS[shape] | set(SHAPE_RIDERS)
    for piece in text.split(";"):
        piece = piece.strip()
        if not piece:
            continue
        if "=" not in piece:
            raise DataError(f"{where}: shape parameter {piece!r} is not Key=Value")
        key, value = (part.strip() for part in piece.split("=", 1))
        if key not in allowed:
            raise DataError(
                f"{where}: {shape} has no parameter {key!r}. It reads "
                f"{sorted(allowed)}.")
        if key in params:
            raise DataError(f"{where}: parameter {key!r} is given twice")
        if not value:
            raise DataError(f"{where}: parameter {key!r} has no value")
        if key == "Mode":
            if value not in MOVEMENT_MODES:
                raise DataError(
                    f"{where}: Mode is {value!r}, not one of "
                    f"{sorted(MOVEMENT_MODES)}")
        elif key in TEXT_PARAM_VALUES:
            # A CLOSED SET, CHECKED HERE. These read as text like Mode does, so
            # nothing below would have caught a misspelling: "ForcedMovement=Nockdown"
            # would have been stored and then matched no branch in the engine's
            # parser, which is a skill that runs and does not shove.
            if key in SINGLE_VALUED_TEXT_PARAMS and "," in value:
                raise DataError(
                    f"{where}: {key}={value!r} names more than one, and "
                    f"{key} takes one of {sorted(TEXT_PARAM_VALUES[key])}")
            for one in (v.strip() for v in value.split(",")):
                if not one:
                    continue
                if one not in TEXT_PARAM_VALUES[key]:
                    raise DataError(
                        f"{where}: {key}={one!r} is not one of "
                        f"{sorted(TEXT_PARAM_VALUES[key])}")
            params[key] = value
        elif key in TEXT_PARAMS:
            pass  # checked against the effect list by validate_skill_effects
        else:
            try:
                float(value)
            except ValueError:
                raise DataError(
                    f"{where}: parameter {key!r} is {value!r}, not a number"
                ) from None
        params[key] = value

    check_stalker_kit_keys(params, shape, where)
    check_taunt_keys(params, where)
    check_death_skill_keys(params, shape, where)
    return params


def is_on(params: dict[str, str], key: str) -> bool:
    """Whether a flag key is stated and not nought."""
    return key in params and float(params[key]) != 0.0


def check_death_skill_keys(params: dict[str, str], shape: str,
                           where: str) -> None:
    """Refuse the Death skills' keys where they could not do what they say.

    Issue #2885, slice DS2. EACH REFUSAL IS A ROW THAT WOULD RUN AND QUIETLY DO
    SOMETHING ELSE: a permanent raise that still has a lifespan, or that takes
    rather than raises; minions told to lay an effect the row does not name; a
    detonation stated on a move whose blows are struck elsewhere.
    """
    if is_on(params, "Permanent"):
        for clash in ("Duration", "Interval", "Possess"):
            if clash in params and float(params[clash]) != 0.0:
                raise DataError(
                    f"{where}: states Permanent={params['Permanent']} and "
                    f"{clash}={params[clash]}. A permanent raise stands until "
                    f"it is destroyed, so it has no Duration and no rift "
                    f"Interval, and a thrall Possess takes is already its "
                    f"commander's for good")
    if is_on(params, "MinionsLayEffect"):
        if not (params.get("Effect") or "").strip():
            raise DataError(
                f"{where}: states MinionsLayEffect={params['MinionsLayEffect']}"
                f" and names no Effect, so its minions would lay nothing")
        if is_on(params, "Possess"):
            raise DataError(
                f"{where}: states MinionsLayEffect on a summon that possesses. "
                f"A thrall is taken, not raised, and keeps its own blows")
    if is_on(params, "Detonates") and shape == "Movement":
        mode = params.get("Mode", "")
        walks = mode == "Charge" and float(params.get("Duration", "0")) > 0.0
        if mode == "Flicker" or walks:
            raise DataError(
                f"{where}: states Detonates on a {'walked charge' if walks else 'flicker'}. "
                f"A move detonates where its single arrival strikes, and this "
                f"one's blows are struck step by step, so nothing would explode")
    check_kill_fervour_and_fervour_scaling(params, shape, where)
    check_a_lengthened_flicker(params, shape, where)
    check_a_rising_curse(params, shape, where)
    check_a_spending_or_cashing_strike(params, where)
    check_a_bursting_rift_and_a_buff_on_every_blow(params, where)
    check_a_bursting_wound_a_paid_hit_and_an_escape(params, shape, where)
    check_a_coffin_nail(params, shape, where)


def check_a_coffin_nail(params: dict[str, str], shape: str, where: str) -> None:
    """Refuse the Crossbow draft's C1 nails where they would raise nothing. Issue #2885, slice DS5.

    A flying shot stating `CritChanceAgainst` drives a nail into what it lands
    on, each worth that many points of its firer's critical strike chance
    against it, to `CritChanceAgainstMax` in all, until `EffectDuration` passes
    without a new one (`UCataclysmCoffinNails`). So nought or less a nail
    raises nothing; a ceiling with no nails caps nothing, and one below a
    single nail's points would not let one count whole; nails that never lapse
    or lapse at once are not what the key means; and a beam's landing is no
    flying contact, so it drives none.
    """
    if "CritChanceAgainst" in params:
        per_nail = float(params["CritChanceAgainst"])
        if per_nail <= 0.0:
            raise DataError(
                f"{where}: states CritChanceAgainst={params['CritChanceAgainst']}, "
                f"which raises nothing. A nail's critical strike chance has to "
                f"be more than nought")
        if "CritChanceAgainstMax" in params and float(params["CritChanceAgainstMax"]) < per_nail:
            raise DataError(
                f"{where}: states CritChanceAgainstMax="
                f"{params['CritChanceAgainstMax']}, below one nail's "
                f"CritChanceAgainst={params['CritChanceAgainst']}, so not even "
                f"one nail would count whole")
        if float(params.get("EffectDuration", "0")) <= 0.0:
            raise DataError(
                f"{where}: states CritChanceAgainst and no EffectDuration above "
                f"nought, which is how long the nails stay without a new one")
        if shape == "Projectile" and not flies(params):
            raise DataError(
                f"{where}: states CritChanceAgainst on a projectile with no "
                f"Speed and no Arc, a beam, and only a flying shot's contact "
                f"drives a nail")
    elif "CritChanceAgainstMax" in params:
        raise DataError(
            f"{where}: states CritChanceAgainstMax and no CritChanceAgainst, so "
            f"there are no nails to cap")


def check_a_buff_striking_on_a_critical_states_its_damage(params: dict[str, str],
                                                         damage_percent: str,
                                                         where: str) -> None:
    """A buff whose holder's critical strikes strike again states the Damage Percent each strike is worth.

    Issue #2885, the Crossbow draft's C2, slice DS5: the Crossbow's Carrion
    Crows, "a crow dives at the enemy you struck for 40% weapon damage". The
    strike is the row's own Damage Percent (`UCataclysmSelfBuffSkill::
    CriticalStrikeLanded`); left blank it would take its slot's figure, which
    for the Support slot a buff sits in is nought, so every crow would deal
    nothing. A slot is a key and a skill is worth what it is worth (the owner,
    2026-08-22), so the row says it.
    """
    if is_on(params, "StrikesOnCritical") and not (damage_percent or "").strip():
        raise DataError(
            f"{where}: states StrikesOnCritical={params['StrikesOnCritical']} "
            f"and no Damage Percent, so each strike would be worth whatever "
            f"its slot is, nought for a Support. State what each strike deals")


def flies(params: dict[str, str]) -> bool:
    """Whether a Projectile row puts a shot in the air: a `Speed` or an `Arc` above nought, not a beam."""
    return float(params.get("Speed", "0")) > 0.0 or float(params.get("Arc", "0")) > 0.0


def check_a_bursting_wound_a_paid_hit_and_an_escape(params: dict[str, str], shape: str,
                                                   where: str) -> None:
    """Refuse N14's wound burst, N19's Fervour per hit and N18's untouchable walk where they would do nothing.

    Issue #2885, slice DS5. `OnDeath=Burst` bursts a creature that dies while
    the ailment the skill laid still runs on it, so a row naming no `Ailment`
    has no wound, and one stating no share or no reach bursts for nothing or
    at nobody; the share alone, without the burst, says nothing. Fervour for
    each enemy hit is paid at a flying shot's contact, so a beam would pay
    none, and a cap a cast without the payment caps nothing. `Untargetable=1`
    is read by a move that lasts -- a flicker and a walked charge (a `Charge`
    stating a `Duration`) -- and by nothing else, so on any other move or shape
    it would make its caster untouchable for no time at all. `PassesThroughEnemies=1`
    (the Death skills merge: Wraithwalk's "straight through anything in the
    way") is read by a walked charge alone, which already strikes what it
    walks into, so the key adds the passing and no blow.
    """
    if params.get("OnDeath") == "Burst":
        if not (params.get("Ailment") or "").strip():
            raise DataError(
                f"{where}: states OnDeath=Burst and names no Ailment, so it "
                f"lays no wound for the dying to burst while it still runs")
        if float(params.get("BurstPercentOfMaximumHealth", "0")) <= 0.0:
            raise DataError(
                f"{where}: states OnDeath=Burst and no "
                f"BurstPercentOfMaximumHealth above nought, so the burst "
                f"would deal nothing")
        if float(params.get("OnDeathRange", "0")) <= 0.0:
            raise DataError(
                f"{where}: states OnDeath=Burst and no OnDeathRange above "
                f"nought, so the burst would reach nobody")
    elif "BurstPercentOfMaximumHealth" in params:
        raise DataError(
            f"{where}: states BurstPercentOfMaximumHealth="
            f"{params['BurstPercentOfMaximumHealth']} and not OnDeath=Burst, "
            f"so nothing bursts for it")
    if "HitFervour" in params:
        if float(params["HitFervour"]) <= 0.0:
            raise DataError(
                f"{where}: states HitFervour={params['HitFervour']}, which "
                f"pays nothing. Fervour for each enemy hit has to be more "
                f"than nought")
        if shape == "Projectile" and not flies(params):
            raise DataError(
                f"{where}: states HitFervour on a projectile with no Speed "
                f"and no Arc, a beam, and only a flying shot's contact pays "
                f"it")
    if "HitFervourPerCast" in params:
        if "HitFervour" not in params:
            raise DataError(
                f"{where}: states HitFervourPerCast and no HitFervour, so "
                f"there is no payment to cap")
        if float(params["HitFervourPerCast"]) <= 0.0:
            raise DataError(
                f"{where}: states HitFervourPerCast="
                f"{params['HitFervourPerCast']}, a cap that would let no "
                f"payment through")
    if is_on(params, "Untargetable"):
        lasts = shape == "Movement" and (
            params.get("Mode") == "Flicker"
            or (params.get("Mode") == "Charge" and float(params.get("Duration", "0")) > 0.0))
        if not lasts:
            raise DataError(
                f"{where}: a {shape} states Untargetable={params['Untargetable']}, "
                f"which only a flicker and a walked charge (a Charge stating a "
                f"Duration) read, for as long as they run. Anything else would "
                f"make its caster untouchable for no time at all")
    if is_on(params, "PassesThroughEnemies"):
        walked = shape == "Movement" and params.get("Mode") == "Charge" and float(
            params.get("Duration", "0")) > 0.0
        if not walked:
            raise DataError(
                f"{where}: a {shape} states PassesThroughEnemies="
                f"{params['PassesThroughEnemies']}, which only a walked charge "
                f"(a Charge stating a Duration) reads. Another move would pass "
                f"through enemies and also strike the line, which is Hollow "
                f"Step's and not what this key says")


def check_a_bursting_rift_and_a_buff_on_every_blow(params: dict[str, str],
                                                   where: str) -> None:
    """Refuse N12's `CollapseBursts`, N17's `EveryBlow` and N21's `MeleeReachMetres` where they would do nothing.

    Issue #2885, slice DS4. `SHAPE_PARAMS` keeps the first to a Summon and the
    other two to a SelfBuff. Only a rift closes -- a summon spawning one every
    `Interval` over its `Duration`, Open the Rift's pattern -- so a summon that
    is not one would never burst what it raised. A buff laying its riders with
    every blow lays its `Ailment`, `Effect` and `ForcedMovement`, so one naming
    none of them lays nothing. And a buff adding nought or less to its holder's
    melee reach lengthens nothing, which no row means.
    """
    if is_on(params, "CollapseBursts"):
        rift = float(params.get("Interval", "0")) > 0.0 and float(params.get("Duration", "0")) > 0.0
        if not rift:
            raise DataError(
                f"{where}: states CollapseBursts={params['CollapseBursts']} "
                f"and is not a rift (an Interval and a Duration above "
                f"nought), so it never closes and nothing would burst")
    if is_on(params, "EveryBlow"):
        lays = [key for key in ("Ailment", "Effect", "ForcedMovement") if (params.get(key) or "").strip()]
        if not lays:
            raise DataError(
                f"{where}: states EveryBlow={params['EveryBlow']} and no "
                f"Ailment, Effect or ForcedMovement, so its holder's blows "
                f"would lay nothing")
    if "MeleeReachMetres" in params and float(params["MeleeReachMetres"]) <= 0.0:
        raise DataError(
            f"{where}: states MeleeReachMetres={params['MeleeReachMetres']}, "
            f"which lengthens nothing. A buff's added reach has to be more "
            f"than nought")


def check_a_spending_or_cashing_strike(params: dict[str, str],
                                       where: str) -> None:
    """Refuse N13's `ScalingSource=FervourSpent` and N15's `CashInRadius` where they would do nothing. Slice DS4.

    Issue #2885. `SHAPE_PARAMS` keeps `SpendsAllFervour` and `CashInRadius` to
    a Strike. What a use spent is counted only when its row spends: a row
    scaling by `FervourSpent` without `SpendsAllFervour=1` would scale by a
    nought it never moves -- the Wand's Requiem states both. And a cash-in
    reaches the enemies within its radius, so nought or less reaches nobody.
    """
    if params.get("ScalingSource") == "FervourSpent" and not is_on(params, "SpendsAllFervour"):
        raise DataError(
            f"{where}: states ScalingSource=FervourSpent and not "
            f"SpendsAllFervour=1, so nothing is spent and it would scale by "
            f"nought")
    if "CashInRadius" in params and float(params["CashInRadius"]) <= 0.0:
        raise DataError(
            f"{where}: states CashInRadius={params['CashInRadius']}, which "
            f"reaches nobody. A cash-in's radius has to be more than nought")


def check_a_lengthened_flicker(params: dict[str, str], shape: str,
                               where: str) -> None:
    """Refuse N10's keys on a move whose deaths would not lengthen it. Issue #2885, slice DS3.

    `UCataclysmMovementSkill::NoteADeathWhileItRuns` lengthens a running
    flicker that counts deaths -- `Mode=Flicker; ScalingSource=Kill` -- by its
    `DurationPer`, to its `MaxDuration` in all: Danse Macabre. On any other
    move `DurationPer` is read by nothing, and a ceiling with nothing to cap,
    or one below the move's own duration, says something untrue.
    """
    if shape != "Movement":
        return
    lengthens = float(params.get("DurationPer", "0")) > 0.0
    if lengthens and (params.get("Mode") != "Flicker" or params.get("ScalingSource") != "Kill"):
        raise DataError(
            f"{where}: a Movement states DurationPer={params['DurationPer']} "
            f"and is not a flicker counting kills (Mode=Flicker; "
            f"ScalingSource=Kill), the one move whose deaths lengthen it, so "
            f"nothing would read it")
    if "MaxDuration" in params:
        ceiling = float(params["MaxDuration"])
        if not lengthens:
            raise DataError(
                f"{where}: states MaxDuration={params['MaxDuration']} and no "
                f"DurationPer above nought, so there is nothing to cap")
        if ceiling < float(params.get("Duration", "0")):
            raise DataError(
                f"{where}: MaxDuration={params['MaxDuration']} is below the "
                f"move's own Duration={params.get('Duration')}, so the ceiling "
                f"would be lower than where it starts")


def check_a_rising_curse(params: dict[str, str], shape: str,
                         where: str) -> None:
    """Refuse N5's `OnDeath=Rise` and `RisesAs` where nothing would rise. Issue #2885, slice DS3.

    A Debuff's curse marks the creature it lands on
    (`UCataclysmSkillTemplate::ApplyNamedEffectsTo`), and the creature rises
    as `RisesAs` names if it dies while that curse holds. So the two keys go
    together, on a Debuff, which names the curse in its `Effect`. Whether a
    Summon row raises the kind is `validate_risen_kinds`'s to say, since it
    needs the whole table.
    """
    rises = params.get("OnDeath") == "Rise"
    if rises and shape != "Debuff":
        raise DataError(
            f"{where}: a {shape} states OnDeath=Rise, which only a Debuff's "
            f"curse marks a creature for")
    if rises and not (params.get("RisesAs") or "").strip():
        raise DataError(
            f"{where}: states OnDeath=Rise and no RisesAs, so it names no kind "
            f"to rise")
    if "RisesAs" in params and not rises:
        raise DataError(
            f"{where}: states RisesAs={params['RisesAs']} and not OnDeath=Rise, "
            f"so nothing would rise")
    if rises and not RISEN_KIND.match(params["RisesAs"].strip()):
        raise DataError(
            f"{where}: RisesAs={params['RisesAs']!r} is not one Minion Types "
            f"row's name")
    if rises and not (params.get("Effect") or "").strip():
        raise DataError(
            f"{where}: states OnDeath=Rise and names no Effect, so there is no "
            f"curse for a creature to die under")


def check_kill_fervour_and_fervour_scaling(params: dict[str, str], shape: str,
                                           where: str) -> None:
    """Refuse N6's `KillFervour` and N7's `ScalingSource=Fervour` where they would pay or scale nothing.

    Issue #2885, slice DS3. `SHAPE_PARAMS` already keeps `KillFervour` to the
    four shapes whose blows are their own; this refuses what is left. A
    payment below nought takes Fervour for a kill, which no row means. A
    Debuff deals no blow of its own in the Support slot, so its Fervour is for
    a death it marks -- Death Warrant's "if it dies while the warrant holds" --
    and the mark is `OnDeath=SpreadDebuff`; without it the curse pays nothing.
    And `ScalingSource=Fervour` is read by a Strike's blow and a self buff's
    bonus alone (`FERVOUR_SCALING_SHAPES`).
    """
    if "KillFervour" in params:
        amount = float(params["KillFervour"])
        if amount < 0.0:
            raise DataError(
                f"{where}: states KillFervour={params['KillFervour']}, below "
                f"nought, which would take Fervour for a kill")
        if amount > 0.0 and shape == "Debuff" and params.get("OnDeath") != "SpreadDebuff":
            raise DataError(
                f"{where}: a Debuff states KillFervour={params['KillFervour']} "
                f"and not OnDeath=SpreadDebuff. A curse pays for a death it "
                f"marks, and only OnDeath=SpreadDebuff marks one, so this would "
                f"pay nothing")
    if params.get("ScalingSource") == "Fervour" and shape not in FERVOUR_SCALING_SHAPES:
        raise DataError(
            f"{where}: a {shape} states ScalingSource=Fervour, which only a "
            f"Strike's blow and a self buff's bonus read "
            f"({sorted(FERVOUR_SCALING_SHAPES)}), so it would scale nothing")


def check_a_summon_effect_has_a_carrier(params: dict[str, str], shape: str,
                                        damage_percent: str,
                                        where: str) -> None:
    """A Summon row naming an `Effect` says what lays it. Issue #2885, N2 and N11.

    TWO CARRIERS AND NO THIRD. A summon that states a Damage Percent strikes as
    it raises and lays its effect with that blow (N2: Barrow Guard's Cripple);
    one stating `MinionsLayEffect=1` has its minions lay it with their blows
    (N11: Bone Choir's). With neither, the summon template reads the effect
    nowhere and the row's sentence would be false. A possession lays nothing
    of its own either: Subjugate's burn is `Burn=1`.
    """
    if shape != "Summon" or not (params.get("Effect") or "").strip():
        return
    try:
        strikes = bool(damage_percent) and float(damage_percent) > 0.0
    except ValueError:
        strikes = False  # `skill_number` names the unreadable cell
    if is_on(params, "Possess") or not (strikes or is_on(params, "MinionsLayEffect")):
        raise DataError(
            f"{where}: a Summon names Effect={params['Effect']} and nothing "
            f"lays it. It needs a Damage Percent, which makes the raise strike "
            f"and lay it, or MinionsLayEffect=1, and it may not possess")


def check_taunt_keys(params: dict[str, str], where: str) -> None:
    """Refuse a `Taunts` that could not taunt anything. Issue #2885, C3.

    TWO WAYS, EACH A ROW THAT WOULD RUN AND QUIETLY DO NOTHING. A taunt reaches
    the enemies within the row's `Radius` of each minion, so a row stating no
    radius taunts nobody. And a summon that possesses (Subjugate) makes no
    minion of its own: the thrall it takes never passes through
    `UCataclysmSummonSkill::SummonOne`, so nothing would be told to taunt.
    A value of nought is the key switched off and passes.
    """
    if "Taunts" not in params or float(params["Taunts"]) == 0.0:
        return
    if float(params.get("Radius", "0")) <= 0.0:
        raise DataError(
            f"{where}: states Taunts={params['Taunts']} and no Radius above "
            f"nought. A taunt reaches the enemies within the row's Radius of "
            f"each minion, so this one would taunt nobody")
    if float(params.get("Possess", "0")) != 0.0:
        raise DataError(
            f"{where}: states Taunts={params['Taunts']} on a summon that "
            f"possesses. A thrall is taken, not raised, so nothing the row "
            f"makes would be told to taunt")


def check_stalker_kit_keys(params: dict[str, str], shape: str,
                           where: str) -> None:
    """Refuse the four keys of issue #2580 where they could not do what they say.

    EACH REFUSAL IS A ROW THAT WOULD RUN AND QUIETLY DO SOMETHING ELSE, which is
    the failure `parse_shape_params` exists to prevent: a roll with one end, a
    pick with nothing to pick between, a random buff on a shape that grants
    nothing to its caster, an ailment on a blow the row does not deal.
    """
    # [N1] A STATED AILMENT ON A SHAPE WHOSE BLOWS ARE DEALT BY SOMETHING ELSE.
    if "Ailment" in params and shape in SHAPES_WHOSE_BLOWS_ARE_NOT_THEIR_OWN:
        raise DataError(
            f"{where}: a {shape} states Ailment={params['Ailment']}, and its "
            f"blows are dealt by what it puts in the world, not through the "
            f"skill's own HitTargets, so the ailment would reach nothing")

    # [N3] BOTH ENDS OR NEITHER, AND IN ORDER.
    rolled = [k for k in ("DamageRollMin", "DamageRollMax") if k in params]
    if len(rolled) == 1:
        raise DataError(
            f"{where}: states {rolled[0]} without the other end of the roll. "
            f"A roll needs both DamageRollMin and DamageRollMax")
    if rolled:
        least = float(params["DamageRollMin"])
        most = float(params["DamageRollMax"])
        if least < 0.0:
            raise DataError(
                f"{where}: DamageRollMin is {least}, below nought")
        if most <= 0.0 or most < least:
            raise DataError(
                f"{where}: the roll runs from {least} to {most}. "
                f"DamageRollMax must be above nought and not below "
                f"DamageRollMin")

    # [N4] A PICK NEEDS AT LEAST TWO EFFECTS, NONE OF THEM A STUN. The engine
    # draws among the effects `NamedEffectTags` keeps, which leaves out a stun
    # (applied by `StunSeconds` through `ApplyStun`) and every damage over time
    # (`validate_skill_effects` refuses those, since only it holds the sheet).
    named = [n.strip() for n in (params.get("Effect") or "").split(",")
             if n.strip()]
    if "EffectPick" in params:
        if len(named) < 2:
            raise DataError(
                f"{where}: states EffectPick={params['EffectPick']} and names "
                f"{len(named)} effect(s). A pick draws one of several, so "
                f"Effect must name at least two")
        if "Stun" in named:
            raise DataError(
                f"{where}: picks at random among {named}, and a stun is never "
                f"in the draw: it is laid by StunSeconds through ApplyStun")
        if RANDOM_BUFF_EFFECT in named:
            raise DataError(
                f"{where}: picks at random among {named}, and "
                f"{RANDOM_BUFF_EFFECT} is a self buff's draw, not an effect")

    # [N5] A RANDOM BUFF IS A SELF BUFF'S, AND NAMED ALONE.
    if RANDOM_BUFF_EFFECT in named:
        if shape != "SelfBuff":
            raise DataError(
                f"{where}: a {shape} names Effect={RANDOM_BUFF_EFFECT}. Only a "
                f"SelfBuff draws a buff for its caster")
        if len(named) > 1:
            raise DataError(
                f"{where}: names {RANDOM_BUFF_EFFECT} beside {named}. It is "
                f"the buff the row grants and is named alone")


def check_basic_attack(shape: str, params: str, arms_the_holder: bool,
                       is_weapon: bool, where: str) -> None:
    """Refuse a basic attack that is missing, misplaced, or not pure weapon damage.

    THE BASIC ATTACK IS THE ANCHOR. `game/Data/SkillSlots.csv` puts it at 100%
    and every other slot is a percentage of it, so a basic attack carrying a
    burn, a stun or a patch of ground would make the anchor deal more than the
    weapon does and every other slot's percentage would then describe something
    else. That is why the riders are refused here rather than merely discouraged.

    A weapon that grants no attack damage grants no basic attack either, which is
    the Shield and was decided on issue #619. It is asked about through
    `arms_the_holder` rather than by name so a second such weapon behaves the
    same way without an edit here.

    BOTH HALVES OF `is_weapon and arms_the_holder` ARE LOAD-BEARING. Flat attack
    damage is not by itself a weapon: the Vambraces glove base grants 12 of it,
    and a glove has no swing to describe.
    """
    if params and not shape:
        raise DataError(f"{where}: has basic attack parameters but no shape")

    if not shape:
        if is_weapon and arms_the_holder:
            raise DataError(
                f"{where}: is a weapon that grants flat attack damage and has no "
                "basic attack shape. Every armed weapon needs one, or a character "
                "holding it has nothing between its cooldowns. Issue #524.")
        return

    if not is_weapon:
        raise DataError(f"{where}: is not a weapon but states a basic attack. "
                        "Only a weapon supplies one.")
    if not arms_the_holder:
        raise DataError(
            f"{where}: grants no flat attack damage but states a basic attack, "
            "which would deal 100% of nothing. Issue #619.")
    if shape not in BASIC_ATTACK_SHAPES:
        raise DataError(
            f"{where}: basic attack shape is {shape!r}. A basic attack is the "
            f"weapon's own swing, so it is one of {sorted(BASIC_ATTACK_SHAPES)}.")

    parsed = parse_shape_params(params, shape, where)
    riders = sorted(set(parsed) - SHAPE_PARAMS[shape])
    if riders:
        raise DataError(
            f"{where}: basic attack carries {riders}. A basic attack is 100% "
            "weapon damage and nothing else, which is what makes it the anchor "
            "every other slot is measured against.")


def weapon_skills(book) -> list[dict]:
    out = []
    for index, raw in enumerate(book["Weapon Skills"].iter_rows(values_only=True), 1):
        if index == 1 or not raw or not clean(raw[0]):
            continue
        weapon, damage, slot = clean(raw[0]), clean(raw[1]), clean(raw[2])
        name = clean(raw[3])
        shape = clean(raw[6]) if len(raw) > 6 else ""
        params = clean(raw[7]) if len(raw) > 7 else ""
        crit = clean(raw[8]) if len(raw) > 8 else ""
        damage_percent = clean(raw[9]) if len(raw) > 9 else ""
        cooldown = clean(raw[10]) if len(raw) > 10 else ""
        mana_cost = clean(raw[11]) if len(raw) > 11 else ""
        # Issue #2801: a skill's own Fervour cost, Subjugate's 30; blank takes
        # the slot's, as the three columns before it do.
        fervour_cost = clean(raw[12]) if len(raw) > 12 else ""
        where = f"Weapon Skills row {index} ({damage} {weapon} {slot})"

        if shape and shape not in SHAPE_PARAMS:
            raise DataError(f"{where}: shape {shape!r} is not one of "
                            f"{sorted(SHAPE_PARAMS)}")
        if params and not shape:
            raise DataError(f"{where}: has shape parameters but no shape")
        if shape and not name:
            raise DataError(f"{where}: has a shape but no skill name")

        # Parsed and thrown away. The game parses it again from the CSV; this
        # call is here so a bad cell fails generation rather than producing a
        # skill that runs and does nothing.
        if shape:
            parsed = parse_shape_params(params, shape, where)
            check_a_roll_states_no_damage_percent(parsed, damage_percent, where)
            check_a_summon_effect_has_a_carrier(parsed, shape, damage_percent,
                                                where)
            check_a_buff_striking_on_a_critical_states_its_damage(
                parsed, damage_percent, where)

        out.append({"Name": row_name(damage, weapon, slot),
                    "WeaponType": weapon, "DamageType": damage, "Slot": slot,
                    "SkillName": name,
                    "SkillDescription": clean(raw[4]),
                    "Tags": tags_with_slot(clean(raw[5]), slot, where),
                    "Shape": shape,
                    "ShapeParams": params,
                    "CritChancePercent": skill_crit_chance(crit, where),
                    "DamagePercent": skill_number(
                        damage_percent, "damage percentage", where),
                    "Cooldown": skill_number(cooldown, "cooldown", where),
                    "ManaCost": skill_number(mana_cost, "mana cost", where),
                    "FervourCost": skill_number(
                        fervour_cost, "Fervour cost", where)})
    return unique(out, "Weapon Skills")


def check_a_roll_states_no_damage_percent(params: dict[str, str],
                                          damage_percent: str,
                                          where: str) -> None:
    """A row that rolls its damage leaves the Damage Percent cell blank.

    Issue #2580, [N3], a judgement of 2026-11-17 recorded in docs/DECISIONS.md.
    The roll IS the row's damage percent, drawn per use, and
    `UCataclysmSkillTemplate::GetDamagePercent` answers the draw before it
    reads the column. A row stating both would carry a figure the engine never
    deals, which is the shape of fault issue #1142 was opened for.
    """
    if "DamageRollMax" in params and damage_percent:
        raise DataError(
            f"{where}: states a roll from {params.get('DamageRollMin')} to "
            f"{params['DamageRollMax']} and a Damage Percent of "
            f"{damage_percent}. The roll is the row's damage percent; leave "
            f"the Damage Percent cell blank")


#: What the DamagePercent, Cooldown, ManaCost and FervourCost columns hold for a
#: skill that states none of its own, and takes its slot's figure instead.
#:
#: EXCEPT AN AURA'S DAMAGE, which is nought when its row states none. Ruled by
#: the owner on 2026-11-24, issue #2875: an aura deals damage only if its
#: description says so. The game applies it in
#: `UCataclysmSkillTemplate::GetDamagePercent`, so the cell is still written as
#: this marker and the rule stays in one place;
#: tools/tests/test_an_aura_deals_damage_only_where_its_row_states_it.py lists
#: every aura row and what it deals.
#:
#: A SLOT IS A KEY AND A SKILL IS WORTH WHAT IT IS WORTH, decided by the project
#: owner on 2026-08-22 and recorded in docs/DECISIONS.md. Any skill may go in any
#: slot, so a skill taking its damage from whichever slot it happened to be put
#: in would be worth 250% of weapon damage on one key and 400% on another.
#:
#: MOST CELLS ARE BLANK AND THAT IS DELIBERATE. The mechanism landed before the
#: numbers so that nothing changed until a number was written: a blank falls back
#: to the slot's figure, which is what the game did before. Writing the 112
#: designed skills' numbers is the rest of issue #836, and it has started.
#:
#: THIS SAID "EVERY CELL IS BLANK TODAY" AND STOPPED BEING TRUE. Issue #1243.
#: Twelve rows state a damage percentage now. No count is written here any more,
#: because it changes every time a designed skill's number is filled in, and a
#: comment that states one is wrong from the next commit onwards. The column
#: itself is the only place worth reading it from.
#:
#: NOT ZERO, for the same reason the critical strike sentinel is not. A Support
#: skill deals 0% of weapon damage, a skill may have no cooldown, and a skill may
#: be free, so zero has to mean zero in all three.
UNSTATED_SKILL_NUMBER = -1.0


def skill_number(cell: str, what: str, where: str) -> float:
    """A per-skill damage, cooldown, mana or Fervour cost, or -1 when blank.

    Blank is the ordinary case and is not a fault: most rows state no figure of
    their own and take their slot's.

    THIS SAID "EVERY ONE OF THE 398 ROWS IS BLANK TODAY" AND WAS WRONG TWICE
    OVER. Issue #1243. There are more rows than that now, and twelve of them
    state a damage percentage. The count is deliberately not replaced with
    another one; see the comment on `UNSTATED_SKILL_NUMBER` above.
    """
    if not cell:
        return UNSTATED_SKILL_NUMBER

    try:
        value = float(cell)
    except ValueError:
        raise DataError(
            f"{where}: states a {what} of {cell!r}, which is not a number. "
            "Leave the cell blank to take the slot's figure.") from None

    # NEGATIVE IS REFUSED RATHER THAN PASSED THROUGH, because -1 is the sentinel
    # meaning the row says nothing. A row meaning to state -1 of something would
    # be read as stating nothing at all, and nothing would report it.
    if value < 0.0:
        raise DataError(
            f"{where}: states a {what} of {value}. A negative figure is not "
            "meaningful, and -1 already means the row says nothing. Leave the "
            "cell blank instead.")

    return value


#: What the CritChancePercent column holds for a skill that states no critical
#: strike chance of its own, and takes the 5% default instead.
#:
#: NOT ZERO, AND THAT IS THE WHOLE REASON A SENTINEL IS NEEDED. The decision
#: recorded in docs/DECISIONS.md on 2026-08-04 says the 5% is "a default and not
#: a floor: a skill that states 1% gets 1%, which is what lets a skill be
#: designed to crit less than average". A skill designed never to critically
#: strike states 0, so zero has to mean zero. The same convention -1 carries on
#: the Cataclysm.CritRoll console variable, where it means "roll normally".
UNSTATED_CRIT_CHANCE = -1.0


def skill_crit_chance(cell: str, where: str) -> float:
    """The Crit Chance cell as a number, or -1 when the cell is blank.

    Blank is the ordinary case and is not a fault: every row is blank, and a
    skill that says nothing about critical strikes should not have to.

    THE ROW COUNT THAT USED TO BE IN THIS SENTENCE IS GONE. Issue #1243. It said
    398 and there are more than that now. Unlike the damage percentage above,
    "every row" is still true here -- no skill states a critical strike chance of
    its own -- so the claim survives and only the number went.
    """
    if not cell:
        return UNSTATED_CRIT_CHANCE

    try:
        chance = float(cell)
    except ValueError:
        raise DataError(
            f"{where}: states a critical strike chance of {cell!r}, which is not "
            "a number. Leave the cell blank for the 5% default.") from None

    # THE CAP IS 100 AND IT IS HARD. docs/Cataclysm_GDD_v2.md's caps table says
    # "Above 100% it means nothing", and both the model and the engine clamp
    # there -- HARD_CAPS in sim/cataclysm_sim/character.py and CritChanceCap in
    # CataclysmCombatAttributeSet.h. A row stating 150 would be silently clamped
    # to 100, so it is refused here instead. Whether a keystone may lift that cap
    # is an open design question, issue #658; if it is ever lifted, this bound
    # moves with it.
    if not 0.0 <= chance <= 100.0:
        raise DataError(
            f"{where}: states a critical strike chance of {chance}, which is "
            "outside 0 to 100. The cap is hard; see the caps table in "
            "docs/Cataclysm_GDD_v2.md.")

    return chance


def tags_with_slot(written: str, slot: str, where: str) -> str:
    """Add the row's slot tag to its Tags cell, derived from the Slot column.

    WHY IT IS DERIVED AND NOT WRITTEN. The Tags sheet declares a tag for every
    ability slot, and the Weapon Skills sheet used to carry two of them by hand:
    `Slot.Movement` on every Movement row and `Slot.Ultimate` on every Ultimate
    row, with `Slot.Heavy`, `Slot.Special`, `Slot.Support` and `Slot.Aura` on
    nothing at all. The design says increases are scoped by tag and lists slot
    tags among the scopes, so an affix reading "increased Heavy Attack damage"
    would be a modifier scoped to `Slot.Heavy` -- which would have applied to no
    skill in the game, and nothing would have reported it. Issue #156.

    Deriving it rather than writing it into 398 rows means the Slot column is the
    only place a row's slot is stated, so the tag cannot disagree with it. That
    is the same reason `Cataclysm.Input.EveryAbilitySlotHasAGeneratedTag` exists.

    A SLOT TAG WRITTEN BY HAND IS REFUSED, rather than merged. Allowing both
    would put the slot in two places again, which is the thing this removes.
    """
    tags = [t.strip() for t in written.split(",") if t.strip()]

    hand_written = [t for t in tags if t.startswith("Slot.")]
    if hand_written:
        raise DataError(
            f"{where}: carries the slot tag {hand_written[0]!r} in its Tags "
            f"cell. Slot tags come from the Slot column and are added by the "
            f"generator; remove it from the sheet.")

    # A STRIKE IS A MELEE ATTACK. The project owner ruled on 2026-08-26, under
    # issue #999, that a strike is what "melee" means for a weapon skill, and
    # every row then carrying `Type.Strike` was given `Type.Melee`. A row added
    # later without it drops out of every melee-scoped bonus with nothing to say
    # so; `Demonic_Greatsword_Ultimate` did, until issue #944. Refused here so a
    # new one cannot.
    if "Type.Strike" in tags and "Type.Melee" not in tags:
        raise DataError(
            f"{where}: carries Type.Strike and not Type.Melee. A strike is a "
            f"melee attack (issue #999), so add Type.Melee to its Tags cell.")

    if not slot:
        return ", ".join(tags)

    tags.append(f"Slot.{slot}")
    return ", ".join(tags)


def enchantments(book, negative: bool) -> list[dict]:
    """The sheet holds two independent tables side by side.

    Positives occupy columns A-D, negatives F-I. They are NOT paired: the design
    states positives and negatives roll independently, so a row in one table has
    no relationship to the row beside it.
    """
    first = 5 if negative else 0
    # THE SET NUMBER HAS A COLUMN OF ITS OWN, issue #1443, ruled by the project owner on 2026-09-07: K for the
    # benefits, L for the drawbacks. They sit after both tables rather than inside each, so every reader that knows
    # the two tables by position, the made-up workbooks in the tests among them, reads A to J as before.
    set_column = SET_COLUMN_NEGATIVE if negative else SET_COLUMN_POSITIVE
    out = []
    for index, raw in enumerate(book["Enchantments"].iter_rows(values_only=True), 1):
        if index == 1 or not raw or len(raw) <= first + 3:
            continue
        text = clean(raw[first])
        if not text:
            continue
        kind = "Negative" if negative else "Positive"
        rolls_down = clean(raw[first + 4]) if len(raw) > first + 4 else ""
        set_cell = raw[set_column] if len(raw) > set_column else None
        out.append({"Name": row_name(kind, text[:48]),
                    "Effect": text,
                    "EnchantmentType": clean(raw[first + 1]),
                    "Weight": number(raw[first + 2], "Weight", index),
                    "Tags": clean(raw[first + 3]),
                    "IsNegative": "True" if negative else "False",
                    "RollsDown": ",".join(
                        str(place) for place in
                        sorted(ranges_rolling_down(rolls_down, text, index))),
                    "SetId": set_number(set_cell, index)})
    return unique(out, f"Enchantments ({'negative' if negative else 'positive'})")


#: Where the Enchantments sheet keeps each table's set number, counted from 0: column K for the benefits and L for
#: the drawbacks. Issue #1443.
SET_COLUMN_POSITIVE = 10
SET_COLUMN_NEGATIVE = 11


def set_number(cell, index: int) -> str:
    """A set number cell as the table carries it: a whole number, or 0 for a row in no set.

    0 AND NOT BLANK, because the engine imports the column into an `int32` (`FCataclysmEnchantmentRow::SetId`), whose
    default is 0, and a blank cell in a number column is a parse warning there rather than a value.
    """
    if cell is None or (isinstance(cell, str) and not cell.strip()):
        return "0"
    value = float(cell)
    if not value.is_integer() or value < 1:
        raise DataError(f"Enchantments row {index}: the set number {cell!r} is not a whole number of 1 or more.")
    return str(int(value))


def ranges_rolling_down(cell: str, words: str, index: int = 0) -> frozenset[int]:
    """Which of a sentence's ranges roll from their second number to their first.

    A BETTER ROLL GIVES THE BETTER OUTCOME, ruled 2026-10-05 under the owner's
    delegation. A roll of 1 gives a benefit its best figure and a drawback its
    harshest. For most sentences that is the second number of each range, which
    is what a roll of 1 has always given. For a few it is the first: "re-summons
    after 3-6 seconds" is best at 3, and "cannot exceed 40%-60%" is harshest at
    40. THE `Rolls Down` COLUMN NAMES THOSE RANGES BY THEIR PLACE IN THE
    SENTENCE, counted from 1: `1`, `2`, or `1,2`.

    ONE SOURCE, TWO READERS. The hover text rewrites the sentence's ranges and
    the effect row carries the pair the character receives; they are different
    code and neither reads the other. Both read this column, so they cannot
    disagree about which way a range runs. `enchantment_effects` writes a
    marked range's pair in the opposite order to the sentence's, and
    `UCataclysmItemValues::EnchantmentTextAtRoll` rolls a marked range from its
    second number to its first.

    A PLACE THE SENTENCE DOES NOT HAVE IS REFUSED, so a mark cannot name a range
    a reword removed.
    """
    if not cell:
        return frozenset()
    count = len(enchantment_ranges(words))
    places = set()
    for part in cell.split(","):
        part = part.strip()
        # A CELL TYPED AS A NUMBER ARRIVES AS `2.0`.
        if part.endswith(".0"):
            part = part[:-2]
        if not part.isdigit() or not 1 <= int(part) <= count:
            raise DataError(
                f"Enchantments row {index}: Rolls Down is {cell!r} for {words!r}, "
                f"which states {count} range(s). Name each range that rolls "
                f"from its second number to its first by its place in the "
                f"sentence, counted from 1, separated by commas.")
        places.add(int(part))
    return frozenset(places)


#: How many pieces a set's bonus row says it needs, read from its own words.
#: This is the reading `SetPieceThreshold` makes in
#: `game/Source/Cataclysm/Items/CataclysmDropRoll.cpp`.
SET_PIECE_THRESHOLD = re.compile(r"\((\d+)-Piece Bonus", re.IGNORECASE)

#: The highest enchantment weight, a set row's included since issue #1443 moved
#: the set number into a column of its own.
#: `UCataclysmDropRoll::HighestEnchantmentWeight` is the same number.
HIGHEST_ENCHANTMENT_WEIGHT = 4

#: What a bonus row whose words state no threshold is sorted by: no count of
#: worn pieces reaches it. The game uses MAX_int32 for the same reason.
UNREACHABLE_THRESHOLD = 10 ** 9


def enchantment_set_id(row: dict) -> int:
    """Which named set an enchantment row belongs to, or 0 for none.

    THE NUMBER HAS A COLUMN OF ITS OWN, `SetId`, since issue #1443 (the project
    owner's ruling of 2026-09-07). Until then it was the Weight column, 5 to 18,
    on a row typed `Set`. This is the reading `UCataclysmDropRoll::EnchantmentSetId`
    makes: a row not typed `Set`, or with no number, answers 0.
    `validate_enchantment_set_ids` refuses both mismatches, so a 0 here is never a
    set row that lost its number in silence.
    """
    if row["EnchantmentType"].casefold() != "set":
        return 0
    number = str(row.get("SetId", "")).strip()
    return int(number) if number else 0  # "0" is no set, and int("0") is 0


def set_piece_threshold(words: str) -> int | None:
    """How many worn pieces a set bonus's own words ask for, or None.

    "Archon's Aegis (2-Piece Bonus): ..." asks for two. A row that states no
    threshold answers None; the game reads the same text the same way.
    """
    found = SET_PIECE_THRESHOLD.search(words)
    return int(found.group(1)) if found else None


def enemy_modifiers(book) -> list[dict]:
    """A matrix: one column per Cataclysm, each cell "Name: Description"."""
    rows = list(book["Enemy Modifiers"].iter_rows(values_only=True))
    if not rows:
        raise DataError("Enemy Modifiers is empty")

    headers = [clean(h) for h in rows[0]]
    out = []
    for column, header in enumerate(headers):
        if not header:
            continue
        cataclysm = header.replace(" Modifiers", "").strip()
        if cataclysm not in CATACLYSM_TYPES:
            raise DataError(f"Enemy Modifiers column {column + 1}: "
                            f"{header!r} does not name a Cataclysm type")
        for raw in rows[1:]:
            if column >= len(raw):
                continue
            text = clean(raw[column])
            if not text:
                continue
            name, description = split_named(text)
            if not name:
                name = text[:40]
            out.append({"Name": row_name(cataclysm, name),
                        "CataclysmType": cataclysm,
                        "ModifierName": name,
                        "Description": description})
    return unique(out, "Enemy Modifiers")


#: The numeric columns of the Buffs, Debuffs and DoTs sheets, in sheet order,
#: paired with the field each becomes. Read positionally because those three
#: sheets have NO HEADING ROW, which is a documented property of them rather
#: than an oversight: `docs/README.md` says so, `test_docs_readme_sheet_table_
#: is_true.py` names all three in its HEADERLESS set, and giving them one would
#: change every row count in that table.
#:
#: SO THE ORDER OF THIS TUPLE IS THE SCHEMA. Inserting an entry anywhere but the
#: end silently re-reads every column after it, and nothing would report an
#: error -- a duration would arrive as a strength and both would be wrong.
#: Append only, and say what a new column means in `docs/README.md` at the same
#: time, because the sheet itself cannot say.
STATUS_EFFECT_NUMBERS: tuple[tuple[int, str], ...] = (
    (1, "DurationSeconds"),         # column B
    (2, "PercentOfHit"),            # column C
    (3, "Strength"),                # column D
    (4, "StrengthCap"),             # column E
    (5, "DurationCap"),             # column F
    (6, "PercentOfCurrentHealth"),  # column G
    (7, "FlatDamagePerTick"),       # column H
)

#: Column I: which character sheet stat an effect's Strength moves.
#:
#: WHY IT EXISTS. Issue #1144. The Strength column says 10, 25 or 30 and nothing
#: anywhere said what the number was OF. That pairing lived in C++, in
#: `UCataclysmSkillEffects`, and exactly one effect was in it. The issue said
#: the second effect needing one is the point at which this becomes a column,
#: and Abyssal Aura -- which cuts a player's Demonic and War resistances by 25%
#: -- is that second effect.
#:
#: A COMMA-SEPARATED LIST, because an effect may move more than one stat.
#: Abyssal Aura names two resistances.
#:
#: EVERY NAME IS CHECKED HERE, which answers the objection recorded against
#: naming a thing in data rather than giving it a column of its own: "a
#: misspelled basis would silently read as one of the others and a number cannot
#: be misspelled". A misspelling now stops the generator and names the row.
#:
#: EMPTY IS THE ORDINARY ANSWER. Most effects move no stat, and one that does --
#: Cripple -- is deliberately left empty because no enemy attribute is read for
#: speed at all, so its own code applies it from the tag instead. Issue #1152.
#:
#: THIS NAMED WEAKEN AS A SECOND SUCH EFFECT until issue #1256 moved it onto the
#: column. Weaken's reduction does have a reader: `WeaponDamageOf` reads
#: `attack_damage` live on every blow an enemy strikes.
STATUS_EFFECT_STAT_COLUMN = 8

#: Column J: what an effect's Strength is measured in, for the stats column I
#: names.
#:
#: WHY A COLUMN AND NOT A READING OF THE DESCRIPTION. Shred says it reduces a
#: resistance "by 10" and Abyssal Aura says "by 25%", and both subtract that many
#: points, because a resistance is measured in per cent already. Weaken says "by
#: 20%" and means one fifth of the number. Nothing in the wording separates the
#: two cases.
#:
#: EMPTY MEANS POINTS, so every row written before this column existed keeps the
#: behaviour it had.
STATUS_EFFECT_OPERATION_COLUMN = 9

#: What column J may say. Anything else stops the generator and names the row.
STATUS_EFFECT_OPERATIONS = ("", "points", "proportion")

#: The one value in that column that is not a stat name.
#:
#: SHRED'S STAT DEPENDS ON WHOEVER APPLIED IT and so cannot be written down: a
#: Shred from a Demonic skill cuts Demonic resistance and one from a Death skill
#: cuts Death resistance. This token says "the resistance matching the source's
#: own damage type", and an untyped source cuts the generic All Resistance,
#: which is what the C++ has always done.
STAT_OF_SOURCE_ELEMENT = "resistance_of_source_element"

#: Names in that column that the game resolves but no character sheet shows.
#:
#: THE CHECK BELOW IS NARROWER THAN WHAT THE GAME CAN RESOLVE, deliberately.
#: `UCataclysmPlayerClassStats::StatToAttribute` held 80 names when this was
#: written and only 42 of them are among the 46 character sheet stats; the rest
#: are ailment chances, attributes and derived figures. Validating against the
#: sheet keeps a designer from writing anything the sheet does not show, which
#: is right for almost every row.
#:
#: `attack_damage` IS THE EXCEPTION AND IT IS A REAL ATTRIBUTE. A player's blow
#: is priced off the weapon rather than a sheet stat, so nothing puts it on the
#: character sheet -- but an enemy writes it
#: (`CataclysmEnemyCharacter.cpp:1266`) and every blow an enemy strikes reads it
#: through `UCataclysmSkillEffects::WeaponDamageOf`. Weaken moves it. Issue
#: #1256.
#:
#: ADD TO THIS ONLY WITH THE SAME EVIDENCE: the name is in the C++ map, and
#: something reads the attribute. A name that resolves but that nothing reads is
#: an effect that quietly moves nothing, which is the fault this column exists
#: to prevent.
ATTRIBUTES_THAT_ARE_NOT_SHEET_STATS = frozenset({"attack_damage"})


def sheet_stats():
    """The 46 character sheet stat names, from the model that defines them.

    LAZY, LIKE `enemy_stats` BELOW, so importing this module does not need the
    simulation package on the path.
    """
    if str(SIM_ROOT) not in sys.path:
        sys.path.insert(0, str(SIM_ROOT))
    try:
        from cataclysm_sim.character import ALL_STATS
    except ImportError as error:
        raise DataError(
            f"could not import cataclysm_sim.character from {SIM_ROOT}, which "
            f"is where the character sheet's stats are named: {error}") from error
    return frozenset(ALL_STATS)


def status_effect_stats(cell, index: int, sheet: str, known) -> str:
    """Column I, checked name by name against the stats the game actually has.

    Returns the cleaned list, or "" when the cell is empty. Raises naming the
    row when a name is not one of them, because the whole point of a column over
    a pairing buried in C++ is that a designer can write one and find out at
    once whether it landed.
    """
    text = clean(cell)
    if not text:
        return ""

    names = [part.strip() for part in text.split(",") if part.strip()]
    for name in names:
        if name == STAT_OF_SOURCE_ELEMENT:
            continue
        if name in ATTRIBUTES_THAT_ARE_NOT_SHEET_STATS:
            continue
        if name not in known:
            allowed = ", ".join(
                repr(one) for one in sorted(ATTRIBUTES_THAT_ARE_NOT_SHEET_STATS))
            raise DataError(
                f"{sheet} row {index}: {name!r} in column I is not a stat this "
                f"game has. Use one of the 46 character sheet stats, spelled as "
                f"sim/cataclysm_sim/character.py spells them, or "
                f"{STAT_OF_SOURCE_ELEMENT!r} for an effect whose stat depends on "
                f"whoever applied it, or one of the attributes no sheet shows: "
                f"{allowed}.")

    return ", ".join(names)


def status_effect_operation(cell, index: int, sheet: str, stats: str) -> str:
    """Column J, checked against the two operations the game can apply.

    Returns the cleaned value, lowercased, or "" for an empty cell. Raises
    naming the row for anything else, for the same reason column I does: a
    designer writing "percent" should find out at once rather than ship an
    effect that silently subtracts points.

    IT REFUSES A VALUE ON A ROW THAT NAMES NO STAT, because that row has nothing
    to apply an operation to. Writing one there means the stat column was meant
    to be filled and was not, and reading it as harmless would hide that.
    """
    text = clean(cell).strip().lower()
    if not text:
        return ""

    if text not in STATUS_EFFECT_OPERATIONS:
        allowed = ", ".join(repr(one) for one in STATUS_EFFECT_OPERATIONS if one)
        raise DataError(
            f"{sheet} row {index}: {text!r} in column J is not an operation this "
            f"game can apply. Use {allowed}, or leave it empty, which means "
            f"points.")

    if not stats:
        raise DataError(
            f"{sheet} row {index}: column J says {text!r} but column I names no "
            f"stat, so there is nothing for it to apply to. Name the stat, or "
            f"clear column J.")

    return text


def status_effects(book) -> list[dict]:
    """Buffs, Debuffs and DoTs: "Name: Description", then seven optional numbers.

    The first row is data, not a header. Reading it as a header would silently
    drop one effect from each of the three sheets.

    EVERY NUMBER IS OPTIONAL because most rows state none. An empty cell reads
    as zero, and an effect worth zero applies nothing -- which is the honest
    answer for an effect nobody has designed yet, and is what
    `UCataclysmSkillEffects::BurnNumbers` checks for before applying anything.

    COLUMN B IS HOW LONG AN EFFECT LASTS. It was added for Burn, which every one
    of the sixteen designed Demonic skills applies and which stated no duration
    and no damage anywhere in the design -- so a skill reading "sets each one
    alight" applied an effect with no numbers in it.

    THREE COLUMNS ARE ALTERNATIVE WAYS OF SAYING WHAT ONE TICK DEALS, and an
    effect states exactly one of them. All three are per tick and not a total;
    `docs/DECISIONS.md` carries that decision, made on 2026-08-24.

    * `FlatDamagePerTick`, column H, is a plain amount. Bleed, Poison, Disease,
      Burn and Necrosis use it. The project owner chose it over a percent of the
      hit on 2026-08-24, because a percent of the hit multiplies twice -- the hit
      already grows with the difficulty tier and the three damage over time stats
      multiply on top of it -- while a flat amount grows only with those stats
      and so stays level across the eight tiers.
    * `PercentOfHit`, column C, is a percent of the hit that applied the effect.
      NOTHING USES IT AS OF 2026-08-24. It was Burn's base until the decision
      above. Kept rather than removed because a skill stating its own effect is
      the obvious future caller and removing it would churn the struct and two
      C++ tests for no gain.
    * `PercentOfCurrentHealth`, column G, is for an effect measured against the
      target. Only Void Splinter uses it, at 1% a second. It cannot go through
      the ordinary damage over time path, because current health falls between
      ticks and so the per-tick amount is not fixed. Issue #915.

    SEPARATE COLUMNS RATHER THAN ONE COLUMN AND A STRING NAMING ITS BASIS,
    because a misspelled basis would silently read as one of the others and a
    number cannot be misspelled.

    COLUMNS D TO F ARE AN EFFECT'S STRENGTH AND ITS CAPS. They were added for
    issue #904, because four of the eleven ailments could not be written down at
    all without them:

    * `Strength` is the effect's own magnitude in whatever unit it names --
      Cripple's 30% slow, Weaken's 20% damage reduction, Shred's 10 resistance,
      Necrosis's total denial of healing.
    * `StrengthCap` is where that magnitude stops and rolls into duration
      instead, which is the design's rule for every effect that has one. Empty
      means no NUMERIC cap: Shred's cap is the target's own resistance reaching
      zero, which is a property of the target and not a number of this effect's.
    * `DurationCap` is where the duration stops. Only Stun has one, and Stun is
      the one effect whose scaling stops dead rather than rolling over, because
      its magnitude IS its duration and there is nothing to roll into.
    """
    known = sheet_stats()
    out = []
    for sheet, kind in (("Buffs", "Buff"), ("Debuffs", "Debuff"), ("DoTs", "DoT")):
        for index, raw in enumerate(book[sheet].iter_rows(values_only=True), 1):
            if not raw:
                continue
            text = clean(raw[0])
            if not text:
                continue
            name, description = split_named(text)
            if not name:
                name = text[:40]
            row = {"Name": row_name(kind, name), "EffectKind": kind,
                   "EffectName": name, "Description": description}
            for column, field in STATUS_EFFECT_NUMBERS:
                cell = raw[column] if len(raw) > column else None
                row[field] = (number(cell, field, index)
                              if cell is not None else 0.0)

            # COLUMN I, AFTER THE NUMBERS AND NOT AMONG THEM. It is a name
            # rather than a figure, so it cannot go in STATUS_EFFECT_NUMBERS,
            # whose every entry runs through `number`.
            stat_cell = (raw[STATUS_EFFECT_STAT_COLUMN]
                         if len(raw) > STATUS_EFFECT_STAT_COLUMN else None)
            row["MovesStat"] = status_effect_stats(stat_cell, index, sheet,
                                                   known)

            # COLUMN J, AND IT IS CHECKED AGAINST COLUMN I rather than on its
            # own, so an operation written beside an empty stat column stops the
            # generator instead of reaching the game as a no-op.
            operation_cell = (raw[STATUS_EFFECT_OPERATION_COLUMN]
                              if len(raw) > STATUS_EFFECT_OPERATION_COLUMN
                              else None)
            row["MovesStatBy"] = status_effect_operation(
                operation_cell, index, sheet, row["MovesStat"])

            out.append(row)
    return unique(out, "Status Effects")


def gems(book) -> list[dict]:
    """Gem effects and their value at each of the eight rarity tiers.

    The sheet's "Everyday Gemstone" column holds the effect description, and that
    description STATES the Everyday value: "10% chance to apply void splinter"
    means Everyday is 10%. The seven numeric columns that follow are Quality
    through Cataclysmic, and the series continues from the value in the text --
    10%, then 30, 50, 75, 100, 125, 150, 200.

    So all eight tiers have a value; only the first is written in prose. It is
    extracted here so consumers get eight numbers rather than seven and a
    sentence. Every one of the 25 gems states a percentage, which is checked.
    """
    rows = list(book["Gems"].iter_rows(values_only=True))
    tiers = ["Quality", "Superb", "Masterful", "Legendary",
             "Mythical", "Ascendant", "Cataclysmic"]
    out = []
    seen_names: dict[str, int] = {}
    for index, raw in enumerate(rows[1:], start=2):
        if not raw or not clean(raw[0]):
            continue
        name, effect = clean(raw[0]), clean(raw[1])

        # `\d*\.?\d+` and NOT `\d+(?:\.\d+)?`, which is what this was until issue
        # #246. That pattern requires a digit before the decimal point, so on
        # "Increases Magic Find by .5%" it matched "5%" and made the Everyday
        # value 0.05 -- ten times the stated figure, and larger than the six
        # rarity tiers above it. Nothing reported an error; the gem simply
        # shipped with a value its own description contradicted.
        match = re.search(r"(\d*\.?\d+)\s*%", effect)
        if not match:
            raise DataError(f"Gems row {index}: {name!r} states no percentage in "
                            f"its effect text, so the Everyday value cannot be "
                            f"read: {effect!r}")
        everyday = float(match.group(1)) / 100.0

        # A GEM NAME IS WHAT THE PLAYER READS, so two gems cannot share one.
        # `unique` below would silently key the second Gem_Of_Recovery_1 and both
        # rows would import, leaving two different gems on the ground under one
        # name with no way to tell them apart. That is what happened: one raised
        # health regeneration and one reduced cooldowns, for a month (issue #211).
        # Checked here rather than in a test because the generator is the only
        # place that sees both the sheet and the row key.
        if name in seen_names:
            raise DataError(
                f"Gems row {index}: {name!r} is already the name of the gem on "
                f"row {seen_names[name]}. Two gems cannot share a name: a player "
                f"reading it cannot tell which one they picked up, and the row "
                f"keys differ only by a numeric suffix nothing explains.")
        seen_names[name] = index

        # THE STAT AND ITS BUCKET, columns K and L, ruled 2026-11-21 (issue #46, slice G1): the stat a gem's modifier
        # goes to, and whether its sheet value is a flat amount or an increase.
        stat = clean(raw[10]) if len(raw) > 10 else ""
        value_kind = clean(raw[11]) if len(raw) > 11 else ""
        if not stat or value_kind not in ("flat", "increased"):
            raise DataError(
                f"Gems row {index}: {name!r} has Stat {stat!r} and Value Kind {value_kind!r}. Every gem names the stat "
                f"its modifier goes to, and a Value Kind of 'flat' or 'increased' (issue #46).")

        entry = {"Name": row_name("Gem", name), "GemName": name,
                 "Effect": effect,
                 "GemType": clean(raw[9]) if len(raw) > 9 else "",
                 "Everyday": everyday}
        for offset, tier in enumerate(tiers, start=2):
            entry[tier] = number(raw[offset], tier, index) if offset < len(raw) else 0.0

        # A RARER GEM HAS TO BE WORTH MORE. Gear and gem rarity equal the
        # difficulty tier, so the ladder is the whole of a gem's progression: a
        # tier that pays less than the one below it means a player who finds a
        # rarer gem should keep the commoner one, and nothing in the interface
        # would tell them so.
        #
        # Checked here rather than in a test because the generator is the only
        # place that sees the prose value and the seven numeric ones together.
        # Issue #246: reading ".5%" as 5% put Of The Goblin's Everyday value
        # above its next six tiers, and the table shipped that way because no
        # check compared them.
        ladder = [entry[t] for t in ("Everyday", *tiers)]
        for lower, higher in zip(ladder, ladder[1:], strict=False):
            if higher <= lower:
                raise DataError(
                    f"Gems row {index}: {name!r} does not get better as it gets "
                    f"rarer. Its eight values are {ladder}, and {higher} is not "
                    f"above {lower}. The first of those is read from the effect "
                    f"text {effect!r} and the rest are the sheet's numeric "
                    f"columns.")

        entry["Stat"] = stat
        entry["ValueKind"] = value_kind
        out.append(entry)
    return unique(out, "Gems")


def parse_tier(value, effect: str, field: str, row: int) -> tuple[str, float, float]:
    """Read one city-upgrade tier cell as (kind, value, interval).

    The sheet has no column saying which kind a cell is, so it is inferred from
    the cell's own notation and the effect text. The four kinds, as stated by the
    project owner:

      "10/10%"  IntervalPercent  two values at once. The effect reads "every X
                                 days ... Y%", and the tier improves BOTH: the
                                 interval drops and the magnitude rises. Here
                                 interval 10 days, magnitude 10%.
      "3x"      Multiplier       multiplies the effect.
      "0.3"     Percent          a percentage increase, stored as a fraction.
      "10"      Flat             a flat improvement, in whatever unit the effect
                                 names -- days, floors, a count of dungeons.

    Percent and Flat are told apart by whether the effect text mentions a
    percentage, because both are bare numbers. That is a heuristic on prose, and
    it is why an explicit kind column in the sheet would be better.
    """
    text = clean(value)
    if not text:
        return "", 0.0, 0.0

    # Must be tested before the others: this cell contains a percent sign AND a
    # bare number, so either of the later branches would match it and lose half
    # the value.
    if "/" in text:
        interval_text, _, magnitude_text = text.partition("/")
        magnitude_text = magnitude_text.strip().rstrip("%")
        try:
            interval = float(interval_text.strip())
            magnitude = float(magnitude_text) / 100.0
        except ValueError as error:
            raise DataError(f"City Upgrades row {row}: {field} is {text!r}, which "
                            f"is not 'days/percent'") from error
        return "IntervalPercent", magnitude, interval

    if text.lower().endswith("x"):
        try:
            return "Multiplier", float(text[:-1]), 0.0
        except ValueError as error:
            raise DataError(f"City Upgrades row {row}: {field} is {text!r}, which "
                            f"is not a multiplier") from error

    try:
        amount = float(text)
    except ValueError as error:
        raise DataError(f"City Upgrades row {row}: {field} is {text!r}, which is "
                        f"none of a percentage, a flat number, a multiplier or "
                        f"a days/percent pair") from error

    return ("Percent" if "%" in effect else "Flat"), amount, 0.0


#: "Every 20 days this city's defenses heal 5%." THE INTERVAL COMES FIRST AND THE
#: MAGNITUDE SECOND, so taking the first number in the sentence gives 20 where
#: 0.05 is wanted. Matched before every other shape for that reason. Non-greedy
#: in the middle so the nearest percentage wins rather than a later one.
TIER_ONE_INTERVAL = re.compile(
    r"every\s+(\d+(?:\.\d+)?)\s+days?\b.*?(\d+(?:\.\d+)?)\s*%", re.IGNORECASE)

#: "Dungeons here provide 2x more experience". No space before the x, which is
#: the notation the tier 2 and tier 3 cells use as well.
TIER_ONE_MULTIPLIER = re.compile(r"(\d+(?:\.\d+)?)x\b", re.IGNORECASE)

#: "Increase max defense by 20%".
TIER_ONE_PERCENT = re.compile(r"(\d+(?:\.\d+)?)\s*%")

#: "Dungeons here have 5 more floors." THE FIRST NUMBER, because two rows end
#: with "to a minimum of 1" and that 1 is a floor under the effect rather than
#: the effect's own magnitude.
TIER_ONE_FLAT = re.compile(r"(\d+(?:\.\d+)?)")


def parse_tier_one(effect: str, row: int) -> tuple[str, float, float]:
    """Read a city upgrade's tier 1 magnitude out of its effect sentence.

    THE SHEET HAS NO TIER 1 VALUE COLUMN. Its four columns are Type, Tier 1,
    Tier 2 and Tier 3, and the `Tier 1` cell is the effect sentence itself --
    "Increase max defense by 20%" -- while tiers 2 and 3 are bare numbers beside
    it. So the magnitude an upgrade STARTS at was only ever written in English,
    and no code could read it. Issue #1262.

    The kinds are `parse_tier`'s kinds and mean the same things, so a reader can
    treat all three tiers alike:

      "Every 20 days ... heal 5%."      IntervalPercent  interval 20, value 0.05
      "provide 2x more experience"      Multiplier       value 2
      "Increase max defense by 20%"     Percent          value 0.20
      "have 5 more floors."             Flat             value 5

    THE ORDER OF THE FOUR TESTS IS THE WHOLE FUNCTION. Every later shape also
    matches the sentences the earlier ones catch: an interval sentence contains a
    percentage, and every sentence of any kind contains a bare number. Reordering
    them silently produces a wrong number rather than an error.

    This is a heuristic on prose, exactly as `parse_tier`'s Percent-or-Flat
    decision is, and it is why a real tier 1 value column in the sheet would be
    better. Every parsed value is pinned in
    `tools/tests/test_city_upgrade_tier_one.py`, so a sentence reworded in the
    workbook fails there rather than shipping a changed number.
    """
    text = clean(effect)
    if not text:
        return "", 0.0, 0.0

    match = TIER_ONE_INTERVAL.search(text)
    if match:
        return ("IntervalPercent", float(match.group(2)) / 100.0,
                float(match.group(1)))

    match = TIER_ONE_MULTIPLIER.search(text)
    if match:
        return "Multiplier", float(match.group(1)), 0.0

    match = TIER_ONE_PERCENT.search(text)
    if match:
        return "Percent", float(match.group(1)) / 100.0, 0.0

    match = TIER_ONE_FLAT.search(text)
    if match:
        return "Flat", float(match.group(1)), 0.0

    raise DataError(f"City Upgrades row {row}: Tier 1 is {text!r}, which states "
                    f"no magnitude at all, so there is no number to publish")


def city_upgrades(book) -> list[dict]:
    """City upgrades and what each is worth at tiers 1, 2 and 3.

    A trailing asterisk on the branch name marks a ONE-TIME USE upgrade: it fires
    once and is spent, rather than being a standing improvement. The asterisk is
    turned into a flag here and stripped from the branch name, so nothing has to
    parse punctuation to know how an upgrade behaves.

    One row has no branch. It is also one-time use, has no tiers, and is a last
    resort rather than a city improvement. Which branch it belongs to has not
    been decided, so Branch is left empty and BranchUndecided marks it.

    TIER 1 IS READ OUT OF THE EFFECT SENTENCE, because the sheet's `Tier 1`
    column IS that sentence. See `parse_tier_one` and issue #1262. Until that was
    added, a reader could see what tier 2 would improve a number to and not what
    the number started at, which is enough to block a city upgrade system
    entirely.

    THE PRICE IN GOLD at each upgrade tier, since issue #2742's second slice:
    the Tier 1 Gold, Tier 2 Gold and Tier 3 Gold columns, read by name. A row
    with a tier ladder has all three, above nought and never falling with the
    tier; a row without one (the unbranched last resort) has none, since there
    is no tier to price. They are the price at difficulty tier 1;
    `UCataclysmCityUpgradeMapping::PriceFor` scales them to the run's tier by
    the Gold sheet's pile column. Ruled 2026-11-18 (G4); every figure is a
    judgement.
    """
    rows = list(book["City Upgrades"].iter_rows(values_only=True))
    headers = _header_index(rows, "City Upgrades")
    out = []
    for index, raw in enumerate(rows, 1):
        if index == 1 or not raw:
            continue
        branch, effect = clean(raw[0]), clean(raw[1])
        if not effect:
            continue

        one_time = branch.endswith("*") or not branch
        branch = branch.rstrip("*").strip()

        tier2_cell = clean(raw[2]) if len(raw) > 2 else ""
        tier3_cell = clean(raw[3]) if len(raw) > 3 else ""

        t2_kind, t2_value, t2_interval = parse_tier(
            tier2_cell, effect, "Tier 2", index)
        t3_kind, t3_value, t3_interval = parse_tier(
            tier3_cell, effect, "Tier 3", index)

        # A ROW WITH NO TIER LADDER AT ALL PUBLISHES NO TIER 1 VALUE EITHER.
        # That is one row, the unbranched one, and its sentence is the reason:
        # it cleanses half the dungeons from every city and costs 50% of their
        # defences and population, so it states two magnitudes and one of them is
        # the word "half". Publishing the 50% would say the upgrade's strength is
        # 50% when 50% is what it costs. Keyed on the empty tier cells rather
        # than on the row, so a second tierless upgrade behaves the same way.
        if tier2_cell or tier3_cell:
            t1_kind, t1_value, t1_interval = parse_tier_one(effect, index)
        else:
            t1_kind, t1_value, t1_interval = "", 0.0, 0.0

        prices = _city_upgrade_prices(raw, headers, index, effect,
                                      bool(tier2_cell or tier3_cell))

        out.append({"Name": row_name(branch or "Unbranched", effect[:40]),
                    "Branch": branch,
                    "BranchUndecided": "True" if not branch else "False",
                    "IsOneTimeUse": "True" if one_time else "False",
                    "Effect": effect,
                    # THE EFFECT SENTENCE IS TIER 1'S RAW CELL. The other two
                    # tiers carry a Raw field because their parsed values are
                    # lossy; tier 1's source is the Effect column right here, so
                    # a Tier1Raw would be the same sentence twice in every row.
                    "Tier1Kind": t1_kind,
                    "Tier1Value": t1_value,
                    "Tier1IntervalDays": t1_interval,
                    # The raw cell is kept alongside the parsed values so nothing
                    # is lost if the inference above is ever wrong.
                    "Tier2Raw": tier2_cell,
                    "Tier2Kind": t2_kind,
                    "Tier2Value": t2_value,
                    "Tier2IntervalDays": t2_interval,
                    "Tier3Raw": tier3_cell,
                    "Tier3Kind": t3_kind,
                    "Tier3Value": t3_value,
                    "Tier3IntervalDays": t3_interval,
                    "Tier1Gold": prices[0],
                    "Tier2Gold": prices[1],
                    "Tier3Gold": prices[2]})
    return unique(out, "City Upgrades")


#: The City Upgrades sheet's price columns, upgrade tier 1 to 3. Issue #2742.
CITY_UPGRADE_GOLD_COLUMNS = ("Tier 1 Gold", "Tier 2 Gold", "Tier 3 Gold")


def _city_upgrade_prices(raw, headers, index: int, effect: str,
                         has_ladder: bool) -> list[int]:
    """One city upgrade's price in gold at upgrade tiers 1, 2 and 3.

    A row with a tier ladder must have all three, each a whole number above
    nought and none below the tier before it: a better upgrade cannot cost
    less. A row without a ladder may have a tier 1 price and nothing above it,
    since it is only ever bought at tier 1: the cleanse, priced 40,000 by the
    coordinating session's ruling of 2026-11-22 (issue #2838). With no price
    at all it is priced at nought.
    """
    cells = [_cell(raw, headers, column) for column in CITY_UPGRADE_GOLD_COLUMNS]
    if not has_ladder:
        if any(cells[1:]):
            raise DataError(f"City Upgrades row {index} ({effect!r}) has a price "
                            "above tier 1 but no tier ladder, so there is no "
                            "tier 2 or 3 to price")
        if not cells[0]:
            return [0, 0, 0]
        value = number(cells[0], f"City Upgrades {CITY_UPGRADE_GOLD_COLUMNS[0]}", index)
        if value != int(value) or value <= 0:
            raise DataError(f"City Upgrades row {index} ({effect!r}): "
                            f"{CITY_UPGRADE_GOLD_COLUMNS[0]} is {value:g}; a price "
                            "is a whole number of gold above nought")
        return [int(value), 0, 0]

    prices = []
    for column, cell in zip(CITY_UPGRADE_GOLD_COLUMNS, cells, strict=True):
        value = number(cell, f"City Upgrades {column}", index)
        if value != int(value) or value <= 0:
            raise DataError(f"City Upgrades row {index} ({effect!r}): {column} "
                            f"is {value:g}; a price is a whole number of gold "
                            "above nought")
        prices.append(int(value))
    for tier in range(1, len(prices)):
        if prices[tier] < prices[tier - 1]:
            raise DataError(
                f"City Upgrades row {index} ({effect!r}): "
                f"{CITY_UPGRADE_GOLD_COLUMNS[tier]} ({prices[tier]}) is below "
                f"{CITY_UPGRADE_GOLD_COLUMNS[tier - 1]} ({prices[tier - 1]}); a "
                "higher upgrade tier cannot cost less")
    return prices


def crafting_materials(book) -> list[dict]:
    """The Crafting sheet, which holds two kinds of row.

    EIGHTEEN OF THEM ARE MATERIALS AND NINETEEN ARE CRAFTING ACTIONS. A material
    says which tier it belongs to in its Tier and Source cell -- "Tier 3 (Rare).
    Drop from Dungeon Bosses/Elites." -- and an action such as "Reroll Affix
    Value" says nothing of the kind, because it is a thing you do rather than a
    thing you hold.

    THE TIER IS PUBLISHED AS A NUMBER so the engine does not have to read prose.
    A drop picks a tier and then picks equally among the materials in it, which
    is what `roll_material_tier` in `sim/cataclysm_sim/loot.py` says belongs to
    "whoever holds that table". Zero means the row is an action rather than a
    material, so nothing can drop it.

    AND SO IS AN UPGRADE STONE'S LEVEL, for the same reason and by the same
    route. The ten stones are named "Upgrade Stone +1" through "+10", so the
    level is already stated; publishing it as a number means the engine does not
    parse a name at run time, which it would otherwise do for every material
    that drops. Zero for everything that is not a stone.

    A STONE'S LEVEL IS NOT ITS TIER. The tier is the rarity band it drops in and
    the level is how far it upgrades a piece. The ten were deliberately spread
    two to a band, so a band holds two different levels and neither can be
    derived from the other. Issue #863 is what needs the level: the best stone
    that can drop is capped by the difficulty tier.
    """
    out = []
    for index, raw in enumerate(book["Crafting"].iter_rows(values_only=True), 1):
        if index == 1 or not raw or not clean(raw[0]):
            continue
        name = clean(raw[0])
        # THE MATERIALS END AT THE OPERATIONS TABLE'S OWN HEADER. Its rows are things the forge does, read by
        # `forge_operations`; until #2830 they were read here as materials, the header row included, with an
        # operation's materials, CR impact and base days under TierAndSource, PrimaryUse and Functions.
        if name == FORGE_OPERATIONS_HEADER:
            break
        source = clean(raw[1])
        found = MATERIAL_TIER_AND_SOURCE.match(source or "")
        stone = UPGRADE_STONE_NAME.match(name or "")
        upgrade_level = int(stone.group(1)) if stone else 0
        if upgrade_level > MAX_UPGRADE_STONE_LEVEL:
            raise DataError(
                f"Crafting row {index}: {name!r} names upgrade level "
                f"{upgrade_level}, and gear runs from +1 to "
                f"+{MAX_UPGRADE_STONE_LEVEL}")
        out.append({"Name": row_name("Material", name), "MaterialName": name,
                    "Tier": int(found.group(1)) if found else 0,
                    "UpgradeLevel": upgrade_level,
                    "TierAndSource": source,
                    "PrimaryUse": clean(raw[2]),
                    "Functions": clean(raw[3]),
                    "CRMetric": clean(raw[6]) if len(raw) > 6 else "",
                    "Formula": clean(raw[7]) if len(raw) > 7 else "",
                    "Outcome": clean(raw[8]) if len(raw) > 8 else ""})
    return unique(out, "Crafting")


IMPLICIT_KINDS = ("flat", "increased")
AFFIX_KINDS = ("Stat", "Resistance", "Ailment", "Hybrid")
AFFIX_POSITIONS = ("prefix", "suffix")


#: The workbook columns the generator reads before the sheet has them, by sheet,
#: each with the issue that will add it. Issue #1882.
#:
#: EVERY OTHER COLUMN THE GENERATOR ASKS FOR IS REQUIRED. `_cell` used to
#: return an empty string for a column the sheet did not have, the same as for
#: an empty cell, so a column deleted or renamed in the workbook emptied that
#: field on every row and nothing said so. That is the same fault as a missing
#: row, which the generator already refuses; ruled on 2026-09-23 under the
#: owner's delegation. A generator change may still land before the workbook
#: gains its column, by declaring the column here.
#:
#: THIS TABLE ONLY SHRINKS. An entry whose column the committed workbook now has
#: fails `test_an_optional_column_the_sheet_now_has_is_not_listed`: delete the
#: entry in the same change that adds the column. Empty when written, because
#: every column the generator read was on its sheet (measured 2026-09-23 on
#: `development` at baee8048).
#:
#: TWO ENTRIES SINCE 2026-09-23, read by this generator before the design
#: workbook gains them: Stack Seconds for issue #1833, and Scale Offset for
#: issue #1686. Each leaves with the rows that need it. SCALE OFFSET LEFT with
#: the class point rows, issue #1686's second window. STACK SECONDS LEFT
#: with the seven own-stack enchantments, issue #1833, and the table is
#: empty again.
#:
#: EVERY SECONDS JOINED ON 2026-09-24 for issue #1833's timed grants, and
#: leaves with their rows.
#: EVERY SECONDS LEFT with the timed-grant rows, and the table is empty again.
#:
#: EVERY NTH JOINED ON 2026-09-24 for issue #1833's "every Nth" rows, built
#: ahead of them while the design workbook is with another session, and LEFT
#: with those rows. The table is empty again.
#:
#: MIN POINTS JOINED ON 2026-09-25 for issue #1755's two "At 4 points:" clauses,
#: Set Stance's and Scarred Plate's, built ahead of their rows while the design
#: workbook is with another session. It leaves with those rows.
#: MIN POINTS LEFT with Set Stance's and Scarred Plate's rows.
#: SCALE STEP HIGH JOINED ON 2026-09-25 for issue #1833's kill counter, built
#: ahead of its two rows while the design workbook is with another session,
#: and leaves with those rows.
#: SCALE STEP HIGH LEFT with the kill counter's two rows, and the table is
#: empty again.
#:
#: FERVOUR COST JOINED ON 2026-09-27 for issue #1478, the Ultimate slot's 50
#: Fervour, built ahead of the Skill Slots column while the design workbook is
#: with another session. It leaves when the column is added.
#: FERVOUR COST LEFT with the Skill Slots column, and the table is empty
#: again.
#: STACK SECONDS HIGH JOINED ON 2026-09-30 for issue #1833's two skill-lock
#: rows, built ahead of them while the design workbook is with another
#: session, and leaves with those rows.
#: STACK SECONDS HIGH LEFT with the two skill-lock rows, and the table is
#: empty again.
#: CONDITION 2, CONDITION VALUE 2 AND CONDITION VALUE HIGH JOINED ON
#: 2026-09-30 for issue #1833 group C part 3a, built ahead of their rows
#: while the design workbook is with another session.
#: THEY LEFT with the four condition rows, and the table is empty again.
#: TRIGGER COOLDOWN JOINED ON 2026-09-30 for issue #1833 group D, built ahead
#: of its rows while the design workbook is with another session.
#: IT LEFT with the two random damage over time rows, and the table is empty
#: again.
#: EVENT VALUE JOINED ON 2026-09-30 for issue #1833 group D part 2, built ahead
#: of its rows while the design workbook is with another session.
#: IT LEFT with the health threshold rows, and the table is empty again.
#: AILMENT JOINED ON 2026-10-01 for issue #1833 group D part 4, built ahead of
#: its rows while the design workbook is with another session.
#: IT LEFT with the remaining damage rows, and the table is empty again.
#: SCALE MAX STEPS ON THE PASSIVE EFFECTS SHEET JOINED ON 2026-11-24, for the
#: Ritualist's Wall of Servants ("You take 1% less damage for each minion you
#: have, up to 20% less"), the demonic tree rework the owner approved that day,
#: built ahead of the workbook column while the workbook is with the
#: coordinating session. It leaves when the column is in the workbook, which
#: `tools/tests/test_required_workbook_columns.py` then requires.
#: IT LEFT THE SAME DAY, when `tools/reapply_demonic_tree_rework_workbook.py`
#: added the column with Wall of Servants' 20, and the table is empty again.
#: THE PASSIVE ROW PORT'S FOURTEEN COLUMNS JOINED ON 2026-11-24
#: (`plan_death_and_war.md`, slice B28): the thirteen action columns the
#: Enchantment Effects sheet already has, and Per Point From. Built ahead of
#: their rows, which the War and Death tree slices author; the first slice that
#: writes a row using one adds that column to the workbook with its re-apply
#: script and deletes it here, which `tools/tests/test_required_workbook_columns.py`
#: then requires.
#: PER POINT FROM LEFT ON 2026-11-24 with slice D's rows
#: (`tools/reapply_war_data_rows_workbook.py`), which added the column for Pain
#: is Power's "At 5 points: This bonus also applies to retaliation damage".
#: CONDITION 2 AND CONDITION VALUE 2 LEFT ON 2026-11-25 with the War riders
#: (`tools/reapply_war_riders_workbook.py`), which added the two columns for
#: Hair-Trigger's, Death Blow's and Brute Force's second conditions.
#:
#: ACTION EVENT AND STACK SECONDS LEFT THE SAME DAY with the Saboteur's device
#: conditions (`tools/reapply_war_saboteur_devices_workbook.py`), which added the
#: two columns for Sprung's, Emergency Protocol's and Razor Instincts' own stacks.
#: AND ACTION THE SAME DAY with the Saboteur's rows
#: (`tools/reapply_war_saboteur_rows_workbook.py`): Pressure Sensitive's slow.
#: THE LAST EIGHT LEFT THE SAME DAY with the War vow options' first slice
#: (`tools/reapply_war_vows_v1_workbook.py`), which added all eight -- Fraction
#: Of and Every Seconds for Shield Oath's and Flesh Oath's health, and the six
#: beside them -- so the War line's sheet carries the fourteen the Death line's
#: `tools/reapply_death_trees_workbook.py` had added that day, all required (the
#: coordinating session's ruling). The table is empty again.
OPTIONAL_COLUMNS: dict[str, dict[str, str]] = {}


class _Headers(dict):
    """Header text to column index, remembering which sheet it came from, so a
    missing column can be named with its sheet."""

    def __init__(self, sheet: str, columns: dict[str, int]):
        super().__init__(columns)
        self.sheet = sheet


def _header_index(rows: list, sheet: str) -> dict[str, int]:
    """Map header text to column index, so a reordered sheet still reads."""
    if not rows:
        raise DataError(f"{sheet} is empty")
    headers = {clean(h): i for i, h in enumerate(rows[0]) if clean(h)}
    if not headers:
        raise DataError(f"{sheet} has no header row")
    return _Headers(sheet, headers)


def _cell(raw, headers: dict[str, int], name: str) -> str:
    """One cell by its column's name, or an empty string for an empty cell.

    A COLUMN THE SHEET DOES NOT HAVE IS REFUSED, unless `OPTIONAL_COLUMNS`
    declares it. Issue #1882. Checked on every read rather than once up front,
    so a column is required exactly when the generator reads it and no list of
    required columns has to be kept beside the code that reads them. A plain
    dict passed as `headers`, as some tests do, carries no sheet and is read as
    before.
    """
    index = headers.get(name)
    if index is None:
        sheet = getattr(headers, "sheet", None)
        if sheet is not None and name not in OPTIONAL_COLUMNS.get(sheet, {}):
            raise DataError(
                f"the {sheet} sheet has no {name!r} column, and the generator "
                f"reads it. A column deleted or renamed in the workbook would "
                f"otherwise empty that field on every row with no error. Put "
                f"the column back, or, if the generator is ahead of the "
                f"workbook, declare it in OPTIONAL_COLUMNS with the issue that "
                f"will add it. Issue #1882.")
        return ""
    if index >= len(raw):
        return ""
    return clean(raw[index])


def item_bases(book) -> list[dict]:
    """One item base per row, with up to two implicits in fixed columns.

    An implicit belongs to the BASE rather than to the slot: a chest built for
    armour and one built for evasion are different bases in the same slot, and
    choosing between them is a decision made before any affix is involved.

    Values are the STATED ones, meaning the fully upgraded figures the design
    document quotes. For a two-handed weapon that is the figure BEFORE the
    two-handed multiplier doubles it, so a Greatsword is 78 here and supplies
    156 in play.
    """
    rows = list(book["Item Bases"].iter_rows(values_only=True))
    headers = _header_index(rows, "Item Bases")

    out = []
    for index, raw in enumerate(rows[1:], start=2):
        name = _cell(raw, headers, "Base Name")
        if not name:
            continue
        slot = _cell(raw, headers, "Slot")
        if not slot:
            raise DataError(f"Item Bases row {index}: {name} has no slot")

        hands = _cell(raw, headers, "Hands")
        if hands and hands not in ("1", "2", "1.0", "2.0"):
            raise DataError(f"Item Bases row {index}: {name} is "
                            f"{hands}-handed; a weapon has 1 or 2")

        # Attacks per second, and only a weapon has one. It is NOT an implicit:
        # a two-handed weapon doubles every implicit it carries, which is right
        # for damage and would make a Greatsword swing twice as fast as a Sword.
        # See issue #120 and WeaponBase.attack_speed in
        # sim/cataclysm_sim/affixes.py.
        attack_speed = _cell(raw, headers, "Attack Speed")
        if hands and not attack_speed:
            raise DataError(
                f"Item Bases row {index}: the {name} is a weapon with no attack "
                "speed. Every weapon needs one, because the weapon is where "
                "that base comes from and an increase to zero is worth nothing.")
        if attack_speed and not hands:
            raise DataError(
                f"Item Bases row {index}: {name} is not a weapon but has an "
                "attack speed. Only a weapon supplies that base.")

        # THE BASIC ATTACK'S SHAPE AND REACH. It lives on the weapon base rather
        # than in the Weapon Skills sheet because it does not vary by damage
        # type: the design document calls it weapon damage itself, so there is
        # one per weapon rather than one per weapon-and-damage-type pair. Decided
        # 2026-08-15 on issue #524, and the design document says the matrix holds
        # one skill per NON-BASIC slot, which putting it here keeps true.
        basic_shape = _cell(raw, headers, "Basic Shape")
        basic_params = _cell(raw, headers, "Basic Shape Params")

        # HOW MANY CELLS THE PIECE OCCUPIES IN THE CARRIED BAG, from the
        # footprint table in docs/Inventory_Screen_Design.md. Issue #855.
        #
        # REQUIRED OF EVERY BASE. A base with no footprint could not be put
        # in the bag at all, and a zero would read as an item that takes no
        # room rather than as a base somebody forgot.
        wide = _cell(raw, headers, "Cells Wide")
        high = _cell(raw, headers, "Cells High")
        for label, figure in (("Cells Wide", wide), ("Cells High", high)):
            if not figure or int(float(figure)) < 1:
                raise DataError(
                    f"Item Bases row {index}: {name} has no {label}. Every\n"
                    "base needs a footprint of at least one cell; see the\n"
                    "footprint table in docs/Inventory_Screen_Design.md.")

        entry = {
            "Name": row_name(slot, name),
            "BaseName": name,
            "Slot": slot,
            "Hands": int(float(hands)) if hands else 0,
            "SubType": _cell(raw, headers, "Sub-Type"),
            "WeaponType": _cell(raw, headers, "Weapon Type"),
            "MaxDamageTypes": int(float(
                _cell(raw, headers, "Max Damage Types") or 0)),
            "AttackSpeed": float(attack_speed) if attack_speed else 0.0,
            "BasicShape": basic_shape,
            "BasicShapeParams": basic_params,
            "CellsWide": int(float(wide)),
            "CellsHigh": int(float(high)),
        }

        implicits = 0
        arms_the_holder = False
        for slot_index in (1, 2):
            stat = _cell(raw, headers, f"Implicit {slot_index} Stat")
            kind = _cell(raw, headers, f"Implicit {slot_index} Kind")
            value = _cell(raw, headers, f"Implicit {slot_index} Value")
            if stat:
                if kind not in IMPLICIT_KINDS:
                    raise DataError(
                        f"Item Bases row {index}: {name} implicit {slot_index} "
                        f"kind is {kind!r}, expected one of {list(IMPLICIT_KINDS)}")
                implicits += 1
            # Read off the implicits rather than off the name, which is what
            # player_damage.armed_weapons_in also does. The Shield is the only
            # weapon today that arms nobody, and naming it here instead would
            # make a second such weapon silently wrong.
            #
            # THE TWO ARE NOT IDENTICAL AND THE DIFFERENCE IS DELIBERATE. That
            # function sums any attack_damage implicit; this one requires the
            # kind to be flat. They agree on today's data, because no weapon
            # grants increased attack damage as an implicit. This is the stricter
            # of the two, because a weapon granting only an increase supplies no
            # damage to increase.
            if stat == "attack_damage" and kind == "flat":
                arms_the_holder = True
            entry[f"Implicit{slot_index}Stat"] = stat
            entry[f"Implicit{slot_index}Kind"] = kind
            entry[f"Implicit{slot_index}Value"] = float(value) if value else 0.0
            # THE WHOLE-NUMBER VALUE AT +0, since issue #2883 (D9 of the approved
            # proposal): what the game upgrades, in place of the stated +10
            # figure divided by 2.5. An implicit with no stat states none.
            at_zero = _cell(raw, headers, f"Implicit {slot_index} At +0")
            if stat:
                entry[f"Implicit{slot_index}AtZero"] = _whole_number(
                    at_zero, f"Item Bases row {index}: {name}'s Implicit "
                             f"{slot_index} At +0")
            elif at_zero:
                raise DataError(f"Item Bases row {index}: {name} states an "
                                f"Implicit {slot_index} At +0 of {at_zero!r} "
                                "for an implicit with no stat")
            else:
                entry[f"Implicit{slot_index}AtZero"] = 0

        if implicits == 0:
            raise DataError(f"Item Bases row {index}: {name} grants nothing, so "
                            "it is not a distinct base")

        check_basic_attack(basic_shape, basic_params, arms_the_holder,
                           bool(hands), f"Item Bases row {index} ({name})")
        out.append(entry)

    return unique(out, "Item Bases")


#: What a Weapon Meshes row puts in the Mesh column to mean "draw nothing".
#:
#: A WORD RATHER THAN AN EMPTY CELL, because an empty cell cannot be told apart
#: from a row somebody started and did not finish. Issue #1125 asked for exactly
#: this distinction: "Drawing nothing is a reasonable answer; drawing nothing
#: silently is not."
DRAWS_NOTHING = "None"

#: Where a weapon mesh path has to start. A mesh outside /Game/ is not in the
#: project's content at all and could never be loaded.
CONTENT_PREFIX = "/Game/"


def weapon_meshes(book) -> list[dict]:
    """Which mesh is drawn in the hand for each weapon base. Source: Weapon Meshes.

    WHY THIS IS A SHEET OF ITS OWN RATHER THAN COLUMNS ON ITEM BASES. Issue
    #1125. A mesh path is an art binding, not a design decision: it changes when
    the art changes and says nothing about how the weapon plays. Item Bases is a
    design sheet and keeping third-party asset paths out of it means a new
    weapons pack moves one sheet rather than editing the design. This follows
    Element Visuals, which is the same shape for the same reason.

    THE ROW KEY MATCHES ITEM BASES, so `FCataclysmItem::Base` looks a row up
    directly with no translation. Both are built by `row_name("Weapon", name)`.

    EVERY WEAPON BASE MUST APPEAR AND NOTHING ELSE MAY. A base with no row would
    silently draw nothing, which is the thing this table exists to make
    impossible, and a row for a base that does not exist is a typo that would
    otherwise never be noticed.
    """
    rows = list(book["Weapon Meshes"].iter_rows(values_only=True))
    headers = _header_index(rows, "Weapon Meshes")

    # Which bases are weapons, read off Item Bases rather than listed here, so
    # adding a weapon base to the design cannot leave this table behind.
    base_rows = list(book["Item Bases"].iter_rows(values_only=True))
    base_headers = _header_index(base_rows, "Item Bases")
    weapon_bases = {
        _cell(raw, base_headers, "Base Name")
        for raw in base_rows[1:]
        if _cell(raw, base_headers, "Slot") == "Weapon"
        and _cell(raw, base_headers, "Base Name")
    }

    out = []
    covered = set()
    for index, raw in enumerate(rows[1:], start=2):
        name = _cell(raw, headers, "Base Name")
        if not name:
            continue

        where = f"Weapon Meshes row {index} ({name})"

        if name not in weapon_bases:
            raise DataError(
                f"{where}: there is no weapon base called {name!r} in the Item\n"
                f"Bases sheet. This table is keyed by base name and every key\n"
                f"has to name a real weapon.")
        if name in covered:
            raise DataError(f"{where}: {name!r} already has a row.")
        covered.add(name)

        mesh = _cell(raw, headers, "Mesh")
        if not mesh:
            raise DataError(
                f"{where}: the Mesh cell is empty. A base that should draw\n"
                f"nothing says {DRAWS_NOTHING!r}, so that a row nobody filled\n"
                f"in can be told apart from a weapon that is meant to be\n"
                f"invisible. The Fist is the designed example: unarmed draws\n"
                f"nothing on purpose.")

        if mesh != DRAWS_NOTHING and not mesh.startswith(CONTENT_PREFIX):
            raise DataError(
                f"{where}: the mesh is {mesh!r}, which does not start with\n"
                f"{CONTENT_PREFIX!r}. A mesh outside the project's content\n"
                f"cannot be loaded. Write {DRAWS_NOTHING!r} to draw nothing.")

        scale = _cell(raw, headers, "Scale")
        try:
            scale_value = float(scale)
        except (TypeError, ValueError) as exc:
            raise DataError(
                f"{where}: the scale is {scale!r}, which is not a number.\n"
                f"Write 1 for a mesh drawn at the size the pack authored."
            ) from exc

        if scale_value <= 0.0:
            raise DataError(
                f"{where}: the scale is {scale_value}. A scale of zero or less\n"
                f"is a weapon that cannot be seen, which is what writing\n"
                f"{DRAWS_NOTHING!r} in the Mesh column is for.")

        out.append({
            "Name": row_name("Weapon", name),
            "BaseName": name,
            # Emptied here rather than carrying the word through, so the game
            # tests one thing -- "is the path empty" -- instead of comparing
            # against a magic word it would have to keep in step with this file.
            "Mesh": "" if mesh == DRAWS_NOTHING else mesh,
            "Scale": scale_value,
        })

    missing = sorted(weapon_bases - covered)
    if missing:
        raise DataError(
            "Weapon Meshes has no row for " + ", ".join(missing) + ".\n"
            "Every weapon base needs one, because a base with no row would\n"
            f"draw nothing without saying so. Write {DRAWS_NOTHING!r} in the\n"
            "Mesh column for a weapon that is meant to be invisible.")

    return unique(out, "Weapon Meshes")


#: The affix tiers, 1 to 7, each read as a whole-number range at +0. Issue #2883.
AFFIX_TIERS = (1, 2, 3, 4, 5, 6, 7)

#: The workbook columns holding them, T1 Min .. T7 Max, and the row fields they
#: become, T1Min .. T7Max, in the same order.
AFFIX_TIER_COLUMNS = tuple(f"T{t} {end}" for t in AFFIX_TIERS for end in ("Min", "Max"))
AFFIX_TIER_FIELDS = tuple(column.replace(" ", "") for column in AFFIX_TIER_COLUMNS)


def _whole_number(text: str, where: str) -> int:
    """A cell that must hold a whole number above zero, refused otherwise."""
    try:
        number = float(text)
    except ValueError as error:
        raise DataError(f"{where} is {text!r}, which is not a number") from error
    if number != int(number) or number < 1:
        raise DataError(f"{where} is {text!r}, and it has to be a whole number "
                        "of at least 1")
    return int(number)


def _affix_tier_ranges(raw, headers, index: int, name: str, kind: str) -> tuple[int, ...]:
    """An affix row's seven tier ranges at +0, as fourteen whole numbers, T1 Min first.

    THE RULES ARE THE APPROVED PROPOSAL'S, issue #2883 (the owner, 2026-11-24:
    "Approve as written (Recommended)"):
      - every number is a whole number of at least 1, so what a player is handed
        is a whole number at +0 and the upgrade rule keeps it one;
      - a tier holds at least two values, so the reroll and perfect crafting
        materials always have something to act on;
      - tiers do not overlap: a tier starts above where the one below it ends,
        so a higher tier is never worse than a lower one at any roll.
    A hybrid states none: its two halves are other rows and carry their own.
    """
    cells = [_cell(raw, headers, column) for column in AFFIX_TIER_COLUMNS]
    if kind == "Hybrid":
        if any(cells):
            raise DataError(f"Affixes row {index}: the hybrid {name} states tier "
                            "ranges, and a hybrid's halves carry their own")
        return tuple(0 for _ in AFFIX_TIER_COLUMNS)
    numbers = tuple(_whole_number(text, f"Affixes row {index}: {name}'s {column}")
                    for text, column in zip(cells, AFFIX_TIER_COLUMNS, strict=True))
    previous_high = 0
    for t in AFFIX_TIERS:
        low, high = numbers[2 * (t - 1)], numbers[2 * (t - 1) + 1]
        if high <= low:
            raise DataError(f"Affixes row {index}: {name}'s tier {t} is {low} to "
                            f"{high}, and a tier has to hold at least two values "
                            "or the reroll material has nothing to do")
        if low <= previous_high:
            raise DataError(f"Affixes row {index}: {name}'s tier {t} starts at {low}, "
                            f"at or below where tier {t - 1} ends ({previous_high}), "
                            "and tiers must not overlap")
        previous_high = high
    return numbers


def affixes(book) -> list[dict]:
    """One rollable affix per row, across four kinds.

    Stat        one stat, flat or increased, with a top value
    Resistance  a family covering `Breadth` damage types at `Top Value` each
    Ailment     a chance to apply an effect a gem also applies
    Hybrid      two stat affixes at a reduced share each

    A stat that appears as a prefix never appears as a suffix, which is what
    stops one item carrying four of whatever is strongest.
    """
    rows = list(book["Affixes"].iter_rows(values_only=True))
    headers = _header_index(rows, "Affixes")

    out = []
    for index, raw in enumerate(rows[1:], start=2):
        name = _cell(raw, headers, "Affix Name")
        if not name:
            continue

        kind = _cell(raw, headers, "Affix Kind")
        if kind not in AFFIX_KINDS:
            raise DataError(f"Affixes row {index}: {name} has kind {kind!r}, "
                            f"expected one of {list(AFFIX_KINDS)}")

        position = _cell(raw, headers, "Position")
        if position not in AFFIX_POSITIONS:
            raise DataError(f"Affixes row {index}: {name} is a {position!r}, "
                            f"expected one of {list(AFFIX_POSITIONS)}")

        allowed = _cell(raw, headers, "Allowed Slots")
        if not allowed:
            raise DataError(f"Affixes row {index}: {name} can roll on no slot")

        value = _cell(raw, headers, "Top Value")
        if kind in ("Stat", "Resistance", "Ailment") and not value:
            raise DataError(f"Affixes row {index}: {name} has no top value")

        # A FLOOR MUST SIT BELOW ITS TOP AND ABOVE ZERO. A floor at or above the
        # top would make every tier and every roll produce the same number, and
        # the reroll and perfect crafting materials would have nothing to act
        # on. A negative one would hand the player a penalty.
        floor = _cell(raw, headers, "Floor")
        if floor:
            try:
                floor_value = float(floor)
            except ValueError as error:
                raise DataError(
                    f"Affixes row {index}: {name} has a floor of {floor!r}, "
                    "which is not a number") from error
            top_value = float(value) if value else 0.0
            if floor_value <= 0.0:
                raise DataError(
                    f"Affixes row {index}: {name} has a floor of "
                    f"{floor_value}, and a floor is the worst value a player "
                    "can be handed, so it has to be above zero")
            if floor_value >= top_value:
                raise DataError(
                    f"Affixes row {index}: {name} has a floor of "
                    f"{floor_value} and a top of {top_value}. The floor has to "
                    "be below the top, or every tier and every roll produce "
                    "one number and the reroll material has nothing to do.")

        tiers = _affix_tier_ranges(raw, headers, index, name, kind)

        # THE WORD THIS AFFIX GIVES AN ITEM'S NAME, and only a suffix has one.
        # An item is called `<rarity> <base> of <word>`, so the first word is
        # the rarity: a prefix has nowhere in the name to appear, and a word on
        # one could never be read. A suffix without one leaves an item that
        # rolled it with nothing to be named after. See sim/cataclysm_sim/
        # naming.py, which mirrors this column.
        name_word = _cell(raw, headers, "Name Word")
        if position == "suffix" and not name_word:
            raise DataError(
                f"Affixes row {index}: the suffix {name} has no name word, so "
                "an item rolling it would have nothing to be called after")
        if position == "prefix" and name_word:
            raise DataError(
                f"Affixes row {index}: the prefix {name} carries the name word "
                f"{name_word!r}, and an item's first word is its rarity, so "
                "that word could never appear")

        out.append({
            "Name": row_name(kind, name),
            "AffixName": name,
            "AffixKind": kind,
            "Position": position,
            "Stat": _cell(raw, headers, "Stat"),
            "ValueKind": _cell(raw, headers, "Value Kind"),
            "TopValue": float(value) if value else 0.0,
            "Breadth": int(float(_cell(raw, headers, "Breadth") or 0)),
            "Ailment": _cell(raw, headers, "Ailment"),
            "Gem": _cell(raw, headers, "Gem"),
            "HybridPart1": _cell(raw, headers, "Hybrid Part 1"),
            "HybridPart2": _cell(raw, headers, "Hybrid Part 2"),
            "AllowedSlots": allowed,
            "NameWord": name_word,
            # WHETHER THE STAT IS MEASURED IN PERCENTAGE POINTS, which is a
            # different question from Value Kind's "which bucket of the
            # pipeline does this join". A flat addition to a stat that is
            # itself a percentage is exactly the case that used to print no
            # percent sign, so "+0.2 to life leech" did not say what it meant.
            # Issue #1224.
            "Percent": _cell(raw, headers, "Percent").lower()
                       in ("yes", "true", "1"),
            # THE WORST VALUE A PLAYER CAN BE HANDED, stated rather than
            # derived. Issue #1230.
            #
            # ZERO MEANS NOTHING WAS STATED, and the game then derives the
            # floor the way it always did, as the tier 1 fraction times the
            # bottom of the roll band times an un-upgraded piece. That comes to
            # a tenth of the top, and it is why the bottom of the ladder was
            # worthless on the affixes whose top is itself small. An affix with
            # no floor here is unchanged in every respect.
            "Floor": float(floor) if floor else 0.0,
            # THE SEVEN TIER RANGES AT +0, whole numbers, since issue #2883.
            # What an affix rolls is read from these, not from TopValue and
            # Floor, which stay until issue #2904 retires them. Zero on a
            # hybrid, whose halves carry their own.
            **{column: value for column, value in zip(AFFIX_TIER_FIELDS, tiers,
                                                      strict=True)},
        })

    # TWO AFFIXES SHARING A WORD would make an item's name say less than it
    # looks like it says. A player reading "of Warding" should be able to look
    # it up and find one thing.
    named: dict[str, str] = {}
    for row in out:
        word = row["NameWord"]
        if not word:
            continue
        if word in named:
            raise DataError(f"Affixes: the name word {word!r} is carried by "
                            f"both {named[word]!r} and {row['AffixName']!r}")
        named[word] = row["AffixName"]

    return unique(out, "Affixes")


#: The eight item rarities, weakest first. The Gear Rarity sheet is read in this
#: order, so a rarity missing from the sheet, misspelled there, or listed out of
#: order fails generation rather than producing a ladder the drop cascade walks
#: wrongly.
#:
#: WRITTEN OUT HERE the way CATACLYSM_TYPES is, rather than read from
#: sim/cataclysm_sim/affixes.py. This file keeps the workbook tables and the two
#: model-generated tables apart on purpose, and the rarity ladder belongs to the
#: workbook side. tools/tests/test_generated_loot_tables_match_the_model.py
#: compares this list against the model's RARITIES and against the
#: ECataclysmRarity enum the engine looks these rows up by.
RARITY_LADDER = ("Everyday", "Quality", "Superb", "Masterful",
                 "Legendary", "Mythical", "Ascendant", "Cataclysmic")

#: The highest upgrade level a piece can reach, so a rarity gated above it is a
#: typo rather than a design. Mirrors UCataclysmItemValues::MaxGearLevel and
#: affixes.GEAR_LEVELS.
MAX_GEAR_LEVEL = 10

#: How many hands a weapon takes. Any other value in the Item Sockets sheet is a
#: mistyped cell, since the sheet uses 0 for everything that is not a weapon.
WEAPON_HAND_COUNTS = (1, 2)


def gear_rarity(book) -> list[dict]:
    """One rarity per row: how often it drops, and what a drop of it arrives at.

    THE ROW KEY IS THE RARITY'S OWN NAME, which is also the name of its
    ECataclysmRarity entry. That is the join between the table and the engine:
    the engine walks the ladder by the enum and looks each row up by the enum's
    name, so nothing depends on the order Unreal happens to iterate a DataTable's
    rows in.

    FOUR NUMBERS PER RARITY, and each one is checked against the rarity below it
    because all four move in a known direction as rarity rises:

      Drop Weight        never rises. A rarer thing cannot be more common.
      Gear Level Gate    never falls. Legendary needs +4 and Cataclysmic +10.
      Residue band       neither end falls. A better drop costs more to improve.

    A cell that moves the wrong way is a typo rather than a design, and none of
    them would fail anywhere else: the table would load, the cascade would run,
    and the numbers would simply be wrong.
    """
    rows = list(book["Gear Rarity"].iter_rows(values_only=True))
    headers = _header_index(rows, "Gear Rarity")

    found: dict[str, dict] = {}
    for index, raw in enumerate(rows[1:], start=2):
        rarity = _cell(raw, headers, "Rarity")
        if not rarity:
            continue
        if rarity not in RARITY_LADDER:
            raise DataError(f"Gear Rarity row {index}: {rarity!r} is not a "
                            f"rarity; expected one of {list(RARITY_LADDER)}")
        if rarity in found:
            raise DataError(f"Gear Rarity row {index}: {rarity} appears twice")

        weight = number(_cell(raw, headers, "Drop Weight"), "Drop Weight", index)
        if weight <= 0.0:
            raise DataError(f"Gear Rarity row {index}: {rarity} has a drop "
                            f"weight of {weight:g}, so it can never drop")

        gate = number(_cell(raw, headers, "Gear Level Gate"),
                      "Gear Level Gate", index)
        if not 0 <= gate <= MAX_GEAR_LEVEL:
            raise DataError(f"Gear Rarity row {index}: {rarity} is gated at "
                            f"upgrade level {gate:g}, outside 0 to "
                            f"{MAX_GEAR_LEVEL}")

        lowest = number(_cell(raw, headers, "Residue On Drop Lowest"),
                        "Residue On Drop Lowest", index)
        highest = number(_cell(raw, headers, "Residue On Drop Highest"),
                         "Residue On Drop Highest", index)
        if lowest <= 0.0:
            raise DataError(f"Gear Rarity row {index}: {rarity} drops carrying "
                            f"{lowest:g} residue. Every drop carries some.")
        if highest < lowest:
            raise DataError(f"Gear Rarity row {index}: {rarity}'s residue band "
                            f"runs from {lowest:g} down to {highest:g}")

        # THE COLOUR THE ITEM'S NAME IS DRAWN IN, and the only channel a player
        # has for reading a rarity off the floor at a glance. The design's
        # Interface Colour section names the eight; this column states them.
        # Converted from the sRGB a colour picker shows to the linear an
        # FLinearColor holds, the same way the element visuals are.
        colour = linear_colour(_cell(raw, headers, "Colour"), "Colour",
                               f"Gear Rarity row {index} ({rarity})")

        found[rarity] = {
            "Name": row_name(rarity),
            "Rarity": rarity,
            "DropWeight": weight,
            "GearLevelGate": int(gate),
            "ResidueOnDropLowest": lowest,
            "ResidueOnDropHighest": highest,
            "Colour": colour,
        }

    missing = [rarity for rarity in RARITY_LADDER if rarity not in found]
    if missing:
        raise DataError(f"Gear Rarity has no row for {missing}. Every rarity "
                        "needs one, or a drop rolling it has no weight, no "
                        "gate and no residue.")

    out = [found[rarity] for rarity in RARITY_LADDER]
    for below, above in zip(out[:-1], out[1:], strict=True):
        rising = f"{above['Rarity']} is rarer than {below['Rarity']}, and"
        if above["DropWeight"] > below["DropWeight"]:
            raise DataError(f"Gear Rarity: {rising} drops more often "
                            f"({above['DropWeight']:g} against "
                            f"{below['DropWeight']:g})")
        if above["GearLevelGate"] < below["GearLevelGate"]:
            raise DataError(f"Gear Rarity: {rising} is gated lower "
                            f"(+{above['GearLevelGate']} against "
                            f"+{below['GearLevelGate']})")
        for end in ("ResidueOnDropLowest", "ResidueOnDropHighest"):
            if above[end] < below[end]:
                raise DataError(f"Gear Rarity: {rising} its {end} is smaller "
                                f"({above[end]:g} against {below[end]:g})")

    seen_colours: dict[str, str] = {}
    for row in out:
        if row["Colour"] in seen_colours:
            raise DataError(
                f"Gear Rarity: {row['Rarity']} and {seen_colours[row['Colour']]} "
                "are drawn in the same colour, so a player could not tell them "
                "apart on the floor")
        seen_colours[row["Colour"]] = row["Rarity"]

    return unique(out, "Gear Rarity")


def item_sockets(book) -> list[dict]:
    """The most sockets a piece in each gear slot can have.

    TWO ROWS FOR A WEAPON, because its maximum depends on how many hands it
    takes: three for a one-hander and six for a two-hander, so two one-handed
    weapons match one two-hander. Every other slot has a single row with a hand
    count of 0, which is also how the Item Bases sheet writes a non-weapon.

    POTION SLOTS ARE NOT HERE. They carry one socket each and there are four of
    them, but they are consumables rather than gear and nothing rolls one as a
    drop. They are the difference between the 41 sockets this table describes
    and the 45 the design states across all equipment.
    """
    rows = list(book["Item Sockets"].iter_rows(values_only=True))
    headers = _header_index(rows, "Item Sockets")

    out = []
    seen: set[tuple[str, int]] = set()
    for index, raw in enumerate(rows[1:], start=2):
        slot = _cell(raw, headers, "Slot")
        if not slot:
            continue

        hands = int(number(_cell(raw, headers, "Hands") or 0, "Hands", index))
        if hands and hands not in WEAPON_HAND_COUNTS:
            raise DataError(f"Item Sockets row {index}: {slot} takes {hands} "
                            f"hands; a weapon takes one of "
                            f"{list(WEAPON_HAND_COUNTS)} and everything else 0")

        if (slot, hands) in seen:
            raise DataError(f"Item Sockets row {index}: {slot} at {hands} "
                            "hand(s) appears twice")
        seen.add((slot, hands))

        maximum = number(_cell(raw, headers, "Max Sockets"),
                         "Max Sockets", index)
        if maximum < 1:
            raise DataError(f"Item Sockets row {index}: {slot} holds "
                            f"{maximum:g} sockets, so no gem could ever go in "
                            "one. Remove the slot instead.")

        out.append({
            "Name": row_name(slot, f"{hands}H" if hands else ""),
            "Slot": slot,
            "Hands": hands,
            "MaxSockets": int(maximum),
        })

    return unique(out, "Item Sockets")


def affix_tiers(book) -> list[dict]:
    """How heavily each affix tier is weighted when a drop rolls an affix.

    THE TIERS MUST RUN FROM 1 WITH NO GAP. A drop draws from every tier at or
    below the cap its difficulty tier allows, so a missing tier is not a tier
    that never rolls -- it is a tier the draw has no weight for.
    """
    rows = list(book["Affix Tiers"].iter_rows(values_only=True))
    headers = _header_index(rows, "Affix Tiers")

    out = []
    for index, raw in enumerate(rows[1:], start=2):
        cell = _cell(raw, headers, "Tier")
        if not cell:
            continue
        tier = int(number(cell, "Tier", index))

        weight = number(_cell(raw, headers, "Drop Weight"), "Drop Weight", index)
        if weight <= 0.0:
            raise DataError(f"Affix Tiers row {index}: T{tier} has a drop "
                            f"weight of {weight:g}, so it can never roll")

        out.append({
            "Name": row_name(f"T{tier}"),
            "Tier": tier,
            "DropWeight": weight,
        })

    if [row["Tier"] for row in out] != list(range(1, len(out) + 1)):
        raise DataError("Affix Tiers must list every tier from 1 upward in "
                        f"order, and it lists {[r['Tier'] for r in out]}")

    for below, above in zip(out[:-1], out[1:], strict=True):
        if above["DropWeight"] > below["DropWeight"]:
            raise DataError(f"Affix Tiers: T{above['Tier']} is above "
                            f"T{below['Tier']} and rolls more often "
                            f"({above['DropWeight']:g} against "
                            f"{below['DropWeight']:g})")

    return unique(out, "Affix Tiers")



def enemy_drops(book) -> list[dict]:
    """What a kill of each enemy rarity drops, and how good it is.

    THE ROW KEY IS THE ENEMY RARITY, spelt exactly as `EnemyRarities.csv` keys
    its own rows, so the two tables join on it. That table comes from the
    simulation's enemy model and this one from the workbook, and
    `validate_enemy_drop_rarities` cross-checks them rather than either trusting
    the other.

    FOUR NUMBERS PER RARITY, and each is checked against the rarity below it:

      Gear Drops       never falls. A rarer enemy cannot give less.
      Material Drops   never falls, for the same reason.
      Gold Drops       never falls, for the same reason. Gold piles, not gold:
                       how much a pile holds is the Gold sheet's, by tier and
                       floor. Issue #2742.
      Magic Find       never falls, and a Common enemy adds none at all,
                       because it is the baseline the ladder is read against.

    AN EXPECTED COUNT RATHER THAN A CHANCE for the three drop columns. A chance
    cannot exceed one and a Cataclysm Boss drops twelve items; the fractional
    part is rolled as a probability where the drop happens.
    """
    rows = list(book["Enemy Drops"].iter_rows(values_only=True))
    headers = _header_index(rows, "Enemy Drops")

    out = []
    seen: set[str] = set()
    for index, raw in enumerate(rows[1:], start=2):
        rarity = _cell(raw, headers, "Enemy Rarity")
        if not rarity:
            continue
        if rarity in seen:
            raise DataError(f"Enemy Drops row {index}: {rarity} appears twice")
        seen.add(rarity)

        gear = number(_cell(raw, headers, "Gear Drops"), "Gear Drops", index)
        materials = number(_cell(raw, headers, "Material Drops"),
                           "Material Drops", index)
        for what, value in (("gear", gear), ("crafting material", materials)):
            if value <= 0.0:
                raise DataError(
                    f"Enemy Drops row {index}: a {rarity} drops {value:g} "
                    f"{what} items. Loot quantity multiplies this, so no amount "
                    "of it would make a rate of zero produce anything.")

        # GEMS PER KILL. Issue #46, slice G5: the gear column times the share `sim/analyse_gem_drops.py` measured,
        # `loot.ENEMY_GEM_DROPS`. Zero is refused for the reason above: loot quantity multiplies it.
        gem_drops = number(_cell(raw, headers, "Gem Drops"), "Gem Drops", index)
        if gem_drops <= 0.0:
            raise DataError(
                f"Enemy Drops row {index}: a {rarity} drops {gem_drops:g} gems. Every kill is ruled to have a chance of "
                "a gem, so a rate of zero is a typing mistake.")

        # GOLD PILES PER KILL. Issue #2742, ruled 2026-11-18 (G1): the gear
        # ladder, "as gear and materials drop today". Zero is refused for the
        # reason above, and a rate is never multiplied by loot quantity (G2).
        gold = number(_cell(raw, headers, "Gold Drops"), "Gold Drops", index)
        if gold <= 0.0:
            raise DataError(
                f"Enemy Drops row {index}: a {rarity} drops {gold:g} gold "
                "piles. Every creature's kill is ruled to have a chance of "
                "gold, so a rate of zero is a typing mistake.")

        magic_find = number(_cell(raw, headers, "Magic Find") or 0,
                            "Magic Find", index)
        if magic_find < 0.0:
            raise DataError(
                f"Enemy Drops row {index}: a {rarity} adds {magic_find:g} magic "
                "find. It is an added percentage and cannot be negative.")

        out.append({
            "Name": row_name(rarity),
            "EnemyRarity": rarity,
            "Step": int(number(_cell(raw, headers, "Step"), "Step", index)),
            "GearDrops": gear,
            "MaterialDrops": materials,
            "MagicFind": magic_find,
            "GoldDrops": gold,
            "GemDrops": gem_drops,
        })

    if not out:
        raise DataError("Enemy Drops has no rows")

    if out[0]["MagicFind"] != 0.0:
        raise DataError(
            f"Enemy Drops: the weakest rarity, {out[0]['EnemyRarity']}, adds "
            f"{out[0]['MagicFind']:g} magic find rather than none. It is the "
            "baseline the whole ladder is read against.")

    if [row["Step"] for row in out] != list(range(len(out))):
        raise DataError(
            "Enemy Drops: the Step column has to run from 0 upward in row "
            f"order, and it reads {[row['Step'] for row in out]}")

    for below, above in zip(out[:-1], out[1:], strict=True):
        rising = (f"a {above['EnemyRarity']} is rarer than a "
                  f"{below['EnemyRarity']} and")
        for column, what in (("GearDrops", "gear"),
                             ("MaterialDrops", "crafting materials"),
                             ("GoldDrops", "gold"),
                             ("GemDrops", "gems"),
                             ("MagicFind", "magic find")):
            if above[column] < below[column]:
                raise DataError(
                    f"Enemy Drops: {rising} gives less {what} "
                    f"({above[column]:g} against {below[column]:g})")

    return unique(out, "Enemy Drops")


def material_tiers(book) -> list[dict]:
    """How heavily each crafting material tier is weighted on a drop.

    THE TIERS MUST RUN FROM 1 WITH NO GAP, for the same reason the affix tiers
    must: the roll walks every rung, so a missing one is a rung the cascade has
    no weight for rather than one that never comes up.

    THE MATERIALS COLUMN IS A COUNT, not a list. It says how many crafting
    materials share the tier, which is what turns "one drop in 341 is Extremely
    Rare" into "one drop in 1,023 is Purified Essence" -- the figure the weights
    were actually chosen against. `validate_material_tier_counts` checks it
    against the Crafting sheet, where the materials really are.
    """
    rows = list(book["Material Tiers"].iter_rows(values_only=True))
    headers = _header_index(rows, "Material Tiers")

    out = []
    for index, raw in enumerate(rows[1:], start=2):
        cell = _cell(raw, headers, "Tier")
        if not cell:
            continue
        tier = int(number(cell, "Tier", index))

        name = _cell(raw, headers, "Tier Name")
        if not name:
            raise DataError(f"Material Tiers row {index}: tier {tier} has no "
                            "name, and the Crafting sheet names its tiers")

        weight = number(_cell(raw, headers, "Drop Weight"), "Drop Weight", index)
        if weight <= 0.0:
            raise DataError(f"Material Tiers row {index}: {name} has a drop "
                            f"weight of {weight:g}, so it can never roll")

        materials = number(_cell(raw, headers, "Materials"), "Materials", index)
        if materials < 1:
            raise DataError(f"Material Tiers row {index}: {name} holds "
                            f"{materials:g} materials, so rolling it would give "
                            "nothing")

        # THE COLOUR THE MATERIAL'S NAME IS DRAWN IN ON THE FLOOR. The five
        # tiers are a different ladder from the eight gear rarities and they
        # share a surface with them, so they get their own hue family rather
        # than borrowing five of the gear colours. Decided on 2026-08-19; the
        # reasoning and the measurements are in docs/DECISIONS.md.
        colour = linear_colour(_cell(raw, headers, "Colour"), "Colour",
                               f"Material Tiers row {index} ({name})")

        out.append({
            "Name": row_name(f"T{tier}"),
            "Tier": tier,
            "TierName": name,
            "DropWeight": weight,
            "Materials": int(materials),
            "Colour": colour,
        })

    if [row["Tier"] for row in out] != list(range(1, len(out) + 1)):
        raise DataError("Material Tiers must list every tier from 1 upward in "
                        f"order, and it lists {[r['Tier'] for r in out]}")

    for below, above in zip(out[:-1], out[1:], strict=True):
        if above["DropWeight"] > below["DropWeight"]:
            raise DataError(
                f"Material Tiers: {above['TierName']} is above "
                f"{below['TierName']} and rolls more often "
                f"({above['DropWeight']:g} against {below['DropWeight']:g})")

    # TWO TIERS SHARING A COLOUR WOULD BE TWO THINGS A PLAYER COULD NOT TELL
    # APART on the floor, the same fault the Gear Rarity sheet refuses.
    seen_colours: dict[str, str] = {}
    for row in out:
        if row["Colour"] in seen_colours:
            raise DataError(
                f"Material Tiers: {row['TierName']} and "
                f"{seen_colours[row['Colour']]} are drawn in the same colour, "
                "so a player could not tell them apart on the floor")
        seen_colours[row["Colour"]] = row["TierName"]

    return unique(out, "Material Tiers")


#: How many difficulty tiers a run can be played at, the same eight
#: `UCataclysmEnemyScore::HighestDifficultyTier` holds. The Gold sheet gives each
#: one a row, and a tier with no row would drop piles of nothing.
DIFFICULTY_TIER_COUNT = 8


def gold(book) -> list[dict]:
    """How much a gold pile holds, one row per difficulty tier. Issue #2742.

    A PILE, NOT A KILL. How many piles a kill drops is the Gold Drops column of
    the Enemy Drops sheet; this says how much each one holds, which depends on
    where the kill happened and not on what was killed -- Diablo II's shape, in
    which a rarer monster drops more piles and a pile's value follows the level.

    Gold Per Pile   the pile on floor 1 of a dungeon at this tier, before the
                    0.8 to 1.2 spread a drop is rolled with. Never falls with
                    the tier: a harder tier cannot pay less.
    More Per Floor  the share of the floor 1 pile each floor deeper adds, so
                    floor f holds Gold Per Pile x (1 + More Per Floor x (f - 1)).
                    Depth and reward are the same axis (`CLAUDE.md`), so it is
                    never negative.

    Ruled 2026-11-18 by the coordinating session under the owner's delegation
    of unstated numbers (G1); every figure on the sheet is a judgement, and
    `docs/DECISIONS.md` carries the sources.
    """
    rows = list(book["Gold"].iter_rows(values_only=True))
    headers = _header_index(rows, "Gold")

    out = []
    for index, raw in enumerate(rows[1:], start=2):
        cell = _cell(raw, headers, "Difficulty Tier")
        if not cell:
            continue
        tier = int(number(cell, "Difficulty Tier", index))

        pile = number(_cell(raw, headers, "Gold Per Pile"), "Gold Per Pile",
                      index)
        if pile <= 0.0:
            raise DataError(f"Gold row {index}: tier {tier} holds {pile:g} gold "
                            "in a pile, so its piles would be empty")

        deeper = number(_cell(raw, headers, "More Per Floor"),
                        "More Per Floor", index)
        if deeper < 0.0:
            raise DataError(f"Gold row {index}: tier {tier} adds {deeper:g} a "
                            "floor, so a deeper floor would pay less gold; depth "
                            "and reward are the same axis")

        out.append({
            "Name": row_name(f"Tier {tier}"),
            "DifficultyTier": tier,
            "GoldPerPile": pile,
            "MorePerFloor": deeper,
        })

    tiers = [row["DifficultyTier"] for row in out]
    if tiers != list(range(1, DIFFICULTY_TIER_COUNT + 1)):
        raise DataError(f"Gold must list every difficulty tier from 1 to "
                        f"{DIFFICULTY_TIER_COUNT} in order, and it lists {tiers}")

    for below, above in zip(out[:-1], out[1:], strict=True):
        if above["GoldPerPile"] < below["GoldPerPile"]:
            raise DataError(
                f"Gold: tier {above['DifficultyTier']} is harder than tier "
                f"{below['DifficultyTier']} and its pile holds less "
                f"({above['GoldPerPile']:g} against {below['GoldPerPile']:g})")

    return unique(out, "Gold")


#: The two slots that are allowed to have no cooldown, and the reason each has
#: none. The Basic Attack is automatic, so the weapon's attack speed sets its
#: rate; the Aura is a toggle, so there is nothing to wait for. Any other slot
#: reading zero is a forgotten number rather than a decision, which is exactly
#: how issue #155 went unnoticed while 41 enchantments scaled it.
SLOTS_WITHOUT_A_COOLDOWN = frozenset({"Basic", "Aura"})


def skill_slots(book) -> list[dict]:
    """What a skill in each of the seven slots is worth, waits, and costs.

    WHY THIS SHEET EXISTS. These numbers used to live only in
    sim/cataclysm_sim/character.py, where Unreal cannot reach them, so no
    ability could honour a cooldown or a mana cost. They are per slot rather
    than per skill: no designed skill states its own, and a column on the
    Weapon Skills sheet would be 77 copies of seven values.

    A skill states its own figure only when it differs, which is what Skull
    Splitter does at 500% weapon damage.
    """
    rows = list(book["Skill Slots"].iter_rows(values_only=True))
    headers = _header_index(rows, "Skill Slots")

    out = []
    for index, raw in enumerate(rows[1:], start=2):
        slot = _cell(raw, headers, "Slot")
        if not slot:
            continue

        cooldown = number(_cell(raw, headers, "Cooldown") or 0, "Cooldown", index)
        lowest = number(_cell(raw, headers, "Cooldown Lowest") or 0,
                        "Cooldown Lowest", index)
        highest = number(_cell(raw, headers, "Cooldown Highest") or 0,
                         "Cooldown Highest", index)
        if not lowest <= cooldown <= highest:
            raise DataError(
                f"Skill Slots row {index}: {slot} has a cooldown of {cooldown}s "
                f"outside its own band of {lowest} to {highest}")
        if cooldown == 0.0 and slot not in SLOTS_WITHOUT_A_COOLDOWN:
            raise DataError(
                f"Skill Slots row {index}: {slot} has no cooldown. Only "
                f"{sorted(SLOTS_WITHOUT_A_COOLDOWN)} may have none; the Basic "
                "Attack is automatic and the Aura is a toggle.")

        damage = number(_cell(raw, headers, "Damage Percent") or 0,
                        "Damage Percent", index)
        damage_low = number(_cell(raw, headers, "Damage Lowest") or 0,
                            "Damage Lowest", index)
        damage_high = number(_cell(raw, headers, "Damage Highest") or 0,
                             "Damage Highest", index)
        if not damage_low <= damage <= damage_high:
            raise DataError(
                f"Skill Slots row {index}: {slot} deals {damage}% of weapon "
                f"damage, outside its own band of {damage_low} to {damage_high}")

        fervour_cost = number(_cell(raw, headers, "Fervour Cost") or 0,
                              "Fervour Cost", index)
        if fervour_cost < 0:
            raise DataError(
                f"Skill Slots row {index}: {slot} costs {fervour_cost} Fervour. "
                "A cost below nothing would pay a character for casting.")

        out.append({
            "Name": slot,
            "Slot": slot,
            "DamagePercent": damage,
            "DamageLowest": damage_low,
            "DamageHighest": damage_high,
            "Cooldown": cooldown,
            "CooldownLowest": lowest,
            "CooldownHighest": highest,
            "ManaCost": number(_cell(raw, headers, "Mana Cost") or 0,
                               "Mana Cost", index),
            "ManaOnHit": number(_cell(raw, headers, "Mana On Hit") or 0,
                                "Mana On Hit", index),
            # WHAT ONE USE COSTS IN FERVOUR, ON TOP OF ITS MANA. Issue #1478,
            # ruled 2026-09-09: the Ultimate slot costs 50 and no other slot
            # costs any. A slot figure, like the mana, so every class pays it.
            "FervourCost": fervour_cost,
            "Note": _cell(raw, headers, "Note"),
        })

    return unique(out, "Skill Slots")


#: The row name carrying the stat line every class inherits.
DEFAULT_CLASS_ROW = "Default"


def class_stats(book) -> list[dict]:
    """One (class, stat) pair per row: its level 1 base and its per-level gain.

    THE DEFAULT LINE IS A ROW SET, NOT A SPECIAL CASE. There are 33 stats and 24
    classes planned, so writing every class out in full would be 792 rows of
    which almost all would repeat the same values. A class named "Default"
    carries the shared line and each real class overrides only the stats that
    express its identity, which is also how the design describes a class: as
    much by what it refuses as by what it takes.

    Anything reading this resolves a class's value for a stat by looking for
    that class's row, then the Default row, then zero.
    """
    rows = list(book["Class Stats"].iter_rows(values_only=True))
    headers = _header_index(rows, "Class Stats")

    out = []
    seen: set[tuple[str, str]] = set()
    for index, raw in enumerate(rows[1:], start=2):
        class_name = _cell(raw, headers, "Class")
        if not class_name:
            continue
        stat = _cell(raw, headers, "Stat")
        if not stat:
            raise DataError(f"Class Stats row {index}: {class_name} names no stat")

        key = (class_name, stat)
        if key in seen:
            raise DataError(
                f"Class Stats row {index}: {class_name} sets {stat} twice, so "
                "one of the two is silently ignored")
        seen.add(key)

        out.append({
            "Name": row_name(class_name, stat),
            "ClassName": class_name,
            "Stat": stat,
            "Base": number(_cell(raw, headers, "Base") or 0, "Base", index),
            "PerLevel": number(_cell(raw, headers, "Per Level") or 0,
                               "Per Level", index),
        })

    if not any(r["ClassName"] == DEFAULT_CLASS_ROW for r in out):
        raise DataError(
            f"Class Stats has no {DEFAULT_CLASS_ROW!r} rows, so every class "
            "would start from nothing")
    return unique(out, "Class Stats")


#: The tag each minion family must carry, so a row's family and its tags cannot
#: say two different things.
#:
#: WHICH ATTRIBUTE IS NO LONGER WRITTEN ON THE MINION. Issue #335 settled Spirit
#: for creatures and Agility for machines, and the first version of this table
#: stored that as a `Scaling Attribute` column. That could only ever express one
#: attribute granting one thing. A minion now carries TAGS, and the separate
#: `Minion Scaling` sheet says what each attribute grants to a minion carrying a
#: given tag -- which is the shape Last Epoch uses, where only stats explicitly
#: tagged for minions reach them and the tags are layered rather than one flag.
MINION_FAMILY_TAG = {"Creature": "Minion.Creature", "Machine": "Minion.Machine"}

#: Which enemy a minion picks. The Ballista deliberately targets the furthest,
#: which is the whole reason this is a column rather than one global rule.
MINION_TARGET_MODES = ("Nearest", "Furthest")


def minion_types(book) -> list[dict]:
    """One row per minion type: its own stat block. Issue #336.

    WHY A TABLE OF ITS OWN, rather than numbers in each skill's Shape Params.
    Two skills can produce the same creature -- Summon Imp and Open the Rift both
    make a lesser imp -- and one skill can produce two, as Iron Fortress makes
    ballistae and spike traps. Numbers in the skill row would exist twice for the
    first case and could not be expressed at all for the second. A skill decides
    how many, how often and how long; the type decides what the thing IS.

    HEALTH AND DAMAGE ARE ABSOLUTE, NOT A SHARE OF THE SUMMONER.
    `docs/Cataclysm_GDD_v2.md` states it as a bolded rule: "Every minion type
    has its own stats. A minion is not a percentage of its summoner." Each is
    the type's own base raised by the summoner's level, in the same `Base` and
    `Per Level` shape `Class Stats` already uses for character stats.

    THE SUMMONER'S LEVEL IS THE ONLY THING THAT RAISES THEM. Gear does not cross
    unless a modifier names minions, so a minion's damage rises more slowly than
    its summoner's, whose gear rises too. That gap is what minion affixes and
    points in the scaling attribute exist to close, and it is intended rather
    than a fitting error.
    """
    rows = list(book["Minion Types"].iter_rows(values_only=True))
    headers = _header_index(rows, "Minion Types")

    out = []
    for index, raw in enumerate(rows[1:], start=2):
        name = _cell(raw, headers, "Minion Type")
        if not name:
            continue

        family = _cell(raw, headers, "Family")
        if family not in MINION_FAMILY_TAG:
            raise DataError(
                f"Minion Types row {index}: {name} is family {family!r}; "
                f"expected one of {sorted(MINION_FAMILY_TAG)}")

        written = _cell(raw, headers, "Tags")
        tags = [t.strip() for t in written.split(",") if t.strip()]
        if not tags:
            raise DataError(
                f"Minion Types row {index}: {name} carries no tags, so nothing "
                "can ever scale it")
        required = MINION_FAMILY_TAG[family]
        if required not in tags:
            raise DataError(
                f"Minion Types row {index}: {name} is a {family} and does not "
                f"carry {required}, so no {family} scaling would reach it")
        if "Type.Minion" not in tags:
            raise DataError(
                f"Minion Types row {index}: {name} does not carry Type.Minion")
        for other, tag in MINION_FAMILY_TAG.items():
            if other != family and tag in tags:
                raise DataError(
                    f"Minion Types row {index}: {name} is a {family} and also "
                    f"carries {tag}, so both families' scaling would reach it")
        # `Type.Gadget` IS EVERY DEPLOYABLE MACHINE THAT IS NOT A TRAP, and
        # nothing else: the coordinating session's ruling of 2026-11-25, so a row
        # meaning gadgets alone can name it. `ACataclysmMinion::IsGadget` is the
        # same test in the game.
        is_gadget = "Type.Deployable" in tags and "Type.Trap" not in tags
        if is_gadget != ("Type.Gadget" in tags):
            raise DataError(
                f"Minion Types row {index}: {name} "
                + ("is a deployable machine that is not a trap and does not carry "
                   "Type.Gadget, so a row meaning gadgets would not reach it"
                   if is_gadget else
                   "carries Type.Gadget and is not a deployable machine that is "
                   "not a trap, so a row meaning gadgets would reach it"))

        mode = _cell(raw, headers, "Target Mode")
        if mode not in MINION_TARGET_MODES:
            raise DataError(
                f"Minion Types row {index}: {name} targets {mode!r}; expected "
                f"one of {list(MINION_TARGET_MODES)}")

        interval = number(_cell(raw, headers, "Attack Interval Seconds"),
                          "Attack Interval Seconds", index)
        if interval <= 0:
            raise DataError(
                f"Minion Types row {index}: {name} attacks every {interval}s, "
                "which is never or continuously rather than at a rate")

        health = number(_cell(raw, headers, "Base Health"), "Base Health", index)
        damage = number(_cell(raw, headers, "Base Damage"), "Base Damage", index)
        if health <= 0:
            raise DataError(
                f"Minion Types row {index}: {name} has {health} base health, so "
                "it dies to anything at level 1")
        if damage < 0:
            raise DataError(
                f"Minion Types row {index}: {name} has {damage} base damage")

        threat = number(_cell(raw, headers, "Threat Percent"),
                        "Threat Percent", index)
        if threat < 0:
            raise DataError(
                f"Minion Types row {index}: {name} draws {threat}% attention")

        # WHAT ITS DEATH IS WORTH, AS A SHARE OF ITS OWN BLOW. Issue #1515.
        # The project owner's figure of 2026-09-17: three of the minion's
        # own blows, for every type, machines included, so the Ritualist
        # keystone "Every minion explodes when it dies" stays true of all
        # of them. It replaces the Special slot's 150% of the SUMMONER'S
        # weapon damage, which is what a minion's explosion took until now
        # and what the owner ruled a bug.
        #
        # ZERO IS ALLOWED AND MEANS NO EXPLOSION, which is a type saying so
        # rather than a figure left out: the engine refuses an explosion of
        # nothing anyway.
        explosion = number(_cell(raw, headers, "Explosion Percent Of Own Damage"),
                           "Explosion Percent Of Own Damage", index)
        if explosion < 0:
            raise DataError(
                f"Minion Types row {index}: {name} explodes for {explosion}% "
                "of its own damage, which is less than nothing")

        out.append({
            "Name": name,
            "Family": family,
            "BaseHealth": health,
            "HealthPerLevel": number(_cell(raw, headers, "Health Per Level"),
                                     "Health Per Level", index),
            "BaseDamage": damage,
            "DamagePerLevel": number(_cell(raw, headers, "Damage Per Level"),
                                     "Damage Per Level", index),
            "ExplosionPercentOfOwnDamage": explosion,
            "AttackIntervalSeconds": interval,
            "MoveSpeed": number(_cell(raw, headers, "Move Speed"),
                                "Move Speed", index),
            "ThreatPercent": threat,
            "ReachCm": number(_cell(raw, headers, "Reach Cm"), "Reach Cm", index),
            "NoticeRadiusCm": number(_cell(raw, headers, "Notice Radius Cm"),
                                     "Notice Radius Cm", index),
            "TargetMode": mode,
            "Tags": ", ".join(tags),
        })

    if not out:
        raise DataError("Minion Types has no rows, so no skill can summon "
                        "anything with stats")
    return unique(out, "Minion Types")


#: Which minion stats an attribute may grant. NOT the character sheet: these are
#: the minion's own stats, held in `Minion Types`, and nothing here reaches the
#: summoner.
MINION_SCALABLE_STATS = ("damage", "health")


def minion_scaling(book) -> list[dict]:
    """What one point of an attribute grants a minion carrying a tag.

    WHY THIS IS NOT ROWS IN `Attributes`. That table is (attribute, stat, percent
    per point) with no tag, and everything reading it sums every attribute that
    names a stat. One shared "minion damage" entry would therefore let a
    summoner's Agility raise an imp, which issue #335 settled it must not: the
    attribute is declared per minion type. A tag is what makes the scoping real.

    IT IS THE SHAPE `character.Modifier` ALREADY USES -- a stat, an amount and
    the tags it requires -- which the project owner stated on 2026-08-02: "The
    player holds all of its own increases, and those increases apply to things
    with matching tags." This is that rule pointed at minions, so the four minion
    affixes in issue #337 need no new machinery.

    ONLY DAMAGE IS FILLED IN, because only damage is decided. The design document
    states "Each grants 1.0% increased minion damage per point". Health is
    expressible here and nobody has chosen a figure for it.
    """
    rows = list(book["Minion Scaling"].iter_rows(values_only=True))
    headers = _header_index(rows, "Minion Scaling")

    out = []
    seen: set[tuple[str, str, str]] = set()
    for index, raw in enumerate(rows[1:], start=2):
        attribute = _cell(raw, headers, "Attribute")
        if not attribute:
            continue

        tag = _cell(raw, headers, "Requires Tag")
        if not tag:
            raise DataError(
                f"Minion Scaling row {index}: {attribute} requires no tag, so "
                "it would reach every minion of every family")

        stat = _cell(raw, headers, "Stat")
        if stat not in MINION_SCALABLE_STATS:
            raise DataError(
                f"Minion Scaling row {index}: {attribute} grants {stat!r}; "
                f"expected one of {list(MINION_SCALABLE_STATS)}")

        key = (attribute, tag, stat)
        if key in seen:
            raise DataError(
                f"Minion Scaling row {index}: {attribute} sets {stat} for {tag} "
                "twice, so one of the two is silently ignored")
        seen.add(key)

        value = number(_cell(raw, headers, "Percent Per Point"),
                       "Percent Per Point", index)
        if value <= 0:
            raise DataError(
                f"Minion Scaling row {index}: {attribute} gives {value} percent "
                f"of minion {stat} per point, so the point is wasted")

        out.append({
            "Name": row_name(attribute, tag, stat),
            "Attribute": attribute,
            "RequiresTag": tag,
            "Stat": stat,
            "PercentPerPoint": value,
        })

    if not out:
        raise DataError("Minion Scaling has no rows, so no attribute raises a "
                        "minion at all")
    return unique(out, "Minion Scaling")


def attributes(book) -> list[dict]:
    """What one point of an attribute is worth, one stat per row.

    ATTRIBUTES ONLY EVER SCALE. A point adds to a stat's sum of increases and
    the sum multiplies the base, so an attribute grants nothing on a stat with
    no base. That is the design working rather than failing.

    Values are PERCENT PER POINT: Vitality reads 2, meaning 2% maximum health
    per point. The simulation stores the same figure as a fraction.
    """
    rows = list(book["Attributes"].iter_rows(values_only=True))
    headers = _header_index(rows, "Attributes")

    out = []
    seen: set[tuple[str, str]] = set()
    for index, raw in enumerate(rows[1:], start=2):
        attribute = _cell(raw, headers, "Attribute")
        if not attribute:
            continue
        stat = _cell(raw, headers, "Stat")
        if not stat:
            raise DataError(f"Attributes row {index}: {attribute} names no stat")

        key = (attribute, stat)
        if key in seen:
            raise DataError(
                f"Attributes row {index}: {attribute} sets {stat} twice")
        seen.add(key)

        value = number(_cell(raw, headers, "Percent Per Point"),
                       "Percent Per Point", index)
        if value <= 0:
            raise DataError(
                f"Attributes row {index}: {attribute} gives {value} percent of "
                f"{stat} per point, so the point is wasted")

        out.append({
            "Name": row_name(attribute, stat),
            "Attribute": attribute,
            "Stat": stat,
            "PercentPerPoint": value,
        })
    return unique(out, "Attributes")


#: The prefix every damage type's tag carries. The eight are declared on the
#: Tags sheet and generated into game/Config/Tags/CataclysmTags.ini.
ELEMENT_TAG_PREFIX = "Element."

#: The three material scales, and what one of them being 1.0 means: leave the
#: Niagara system's own authored value alone. None of them may be zero or
#: negative -- see the check in `element_visuals`.
ELEMENT_SCALES = ("EmissiveMultiplier", "SpawnRateScale", "VelocityScale")


def srgb_to_linear(channel: int) -> float:
    """One 0-255 sRGB channel as linear 0-1, the same curve the engine uses.

    The same function as `srgb_to_linear` in tools/generate_telegraph_material.py
    and as `FLinearColor::FromSRGBColor`, which is what
    ACataclysmTelegraphMarker::ResolveColour calls on the design document's hex.
    """
    c = channel / 255.0
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def linear_colour(value, field: str, where: str) -> str:
    """A design document sRGB hex as the text Unreal imports into FLinearColor.

    THE DESIGN STATES sRGB AND THE ENGINE WANTS LINEAR. Section XIII writes
    `#FF7A2E` because that is what a colour picker shows; an FLinearColor is
    linear, so a material fed the raw 0-255 figures divided by 255 renders a
    visibly different colour from the one that was designed. The conversion
    happens here so the workbook keeps the notation a person can copy straight
    out of the design document.

    THE LENGTH IS NOT ENOUGH ON ITS OWN. That was a real bug in this project:
    `FColor::FromHex` does not report bad input, and the word "nonsense" is
    eight characters, so a length-only check accepted it and produced a colour
    nobody asked for. Every character is checked here for the same reason.
    """
    text = clean(value).lstrip("#")
    if not re.fullmatch(r"[0-9A-Fa-f]{6}", text):
        raise DataError(
            f"{where}: {field} is {clean(value)!r}, which is not six hex digits "
            "with an optional leading hash. Section XIII of "
            "docs/Cataclysm_GDD_v2.md states every one of these as sRGB hex.")

    channels = (int(text[i:i + 2], 16) for i in (0, 2, 4))
    red, green, blue = (srgb_to_linear(c) for c in channels)
    return (f"(R={red:.6f},G={green:.6f},B={blue:.6f},A=1.000000)")


def element_visuals(book) -> list[dict]:
    """What each damage type's effects look like. Source: Element Visuals.

    WHY THIS SHEET EXISTS. Issue #549. The eight damage types have an effect
    palette, settled by the project owner and recorded in section XIII of
    docs/Cataclysm_GDD_v2.md, and nothing in the game could read it. Section 5
    of docs/Niagara_Conventions.md specifies this table: eight rows against the
    existing tags, so one Niagara system serves all eight damage types instead
    of eight copies of it. Eight shapes times eight damage types is 64 assets
    built the wrong way and 8 assets plus 8 rows built this one.

    THE ROW KEY IS THE TAG'S LEAF, so `Element.Demonic` keys the row `Demonic`
    and anything holding a tag can reach its row without a lookup table.

    BRIGHTNESS IS NOT CHECKED HERE, deliberately, and this is the one thing
    about this table that is easy to get wrong twice. The design's readability
    rule -- a world surface may not exceed 30% brightness, an effect's primary
    may not fall below 60% -- is already enforced against the design document by
    tools/tests/test_effect_palette_stays_readable.py, and this table is pinned
    to that same document by
    tools/tests/test_element_visuals_match_the_design.py. So the rule reaches
    this table through those two, and a second copy of the CIE lightness formula
    here would be a second thing to keep in step for no extra coverage.
    """
    rows = list(book["Element Visuals"].iter_rows(values_only=True))
    headers = _header_index(rows, "Element Visuals")

    out = []
    for index, raw in enumerate(rows[1:], start=2):
        tag = _cell(raw, headers, "Element Tag")
        if not tag:
            continue

        where = f"Element Visuals row {index} ({tag})"
        if not tag.startswith(ELEMENT_TAG_PREFIX):
            raise DataError(
                f"{where}: the key is {tag!r}, and this table is keyed by a "
                f"damage type's tag, so it must start with "
                f"{ELEMENT_TAG_PREFIX!r}.")

        entry = {
            "Name": row_name(tag[len(ELEMENT_TAG_PREFIX):]),
            "ElementTag": tag,
            "PrimaryColour": linear_colour(
                _cell(raw, headers, "Primary"), "Primary", where),
            "SecondaryColour": linear_colour(
                _cell(raw, headers, "Secondary"), "Secondary", where),
        }

        # A SCALE OF ZERO IS A FORGOTTEN NUMBER, NOT A DECISION, and each of the
        # three fails in a way that looks like a broken effect rather than like
        # bad data: no particles at all, particles that never move, or an effect
        # rendered black. That is the shape of issue #155, where a cooldown of
        # zero went unnoticed across 77 skills because zero read as a value.
        for column, field in zip(("Emissive Multiplier", "Spawn Rate Scale",
                                  "Velocity Scale"), ELEMENT_SCALES,
                                 strict=True):
            scale = number(_cell(raw, headers, column), column, index)
            if scale <= 0:
                raise DataError(
                    f"{where}: {column} is {scale}. A scale of zero or less "
                    "makes the effect invisible rather than neutral; 1.0 is "
                    "what leaves the Niagara system's own value alone.")
            entry[field] = scale

        out.append(entry)

    return unique(out, "Element Visuals")


# --------------------------------------------------------------------------
# the enemy tables, which come from the Python model rather than the workbook

def _enemy_stats():
    """Import `cataclysm_sim.enemy_stats`, adding sim/ to the path if needed.

    Imported here rather than at the top of the file so that generating the
    workbook tables does not depend on the simulation package being importable.

    IMPORTING IT RUNS ITS OWN CHECKS. enemy_stats.py calls five self-checks at
    module scope: every archetype deals a real damage type, none declares more
    resistance than the 70% cap is worth, every body has a width, every creature
    can turn, and no creature's defensive layers combine past what a geared
    player stops. So a model that contradicts itself raises here rather than
    being written out to a CSV.
    """
    if str(SIM_ROOT) not in sys.path:
        sys.path.insert(0, str(SIM_ROOT))
    try:
        from cataclysm_sim import enemy_stats
    except ImportError as error:
        raise DataError(
            f"could not import cataclysm_sim.enemy_stats from {SIM_ROOT}, which "
            f"is where the enemy stat block is designed: {error}") from error
    return enemy_stats


def _six_places(value: float) -> float:
    """Round to six decimal places, so the written table stays readable.

    Python writes the shortest string that reproduces a float exactly, and for a
    rarity multiplier like 0.5 * 1.85 ** 3 that string is 3.1655562500000005.
    Six places is a ten-millionth of a Common enemy's health multiplier, far
    below anything a balance judgement can see, and it keeps the table legible
    to the person reading the diff.

    The drift guard does not depend on this: `--check` rebuilds the whole table
    from the model and compares the text, so changing a model constant makes the
    file stale whatever precision it is written at.
    """
    return round(value, 6)


def enemy_archetypes(_book=None) -> list[dict]:
    """One row per enemy archetype: what KIND of creature it is.

    Source: `ARCHETYPES` in sim/cataclysm_sim/enemy_stats.py. One column per
    field of the `Archetype` dataclass, and no derived values, because deriving
    one here would put a second reading of the design in this file.

    DISTANCES AND SPEEDS ARE IN METRES, as the model states them and as
    game/Data/ClassStats.csv already states the player's movement speed. The
    engine multiplies by `CentimetresPerMetre` when it reads one, the way
    ACataclysmPlayerCharacter::ApplyMovementSpeed does.

    A CHASE SPEED OF 0 IS A SENTINEL, not a creature that cannot move: the model
    defines it as "use MoveSpeed in both states", which is what every enemy but
    the Brute does. It is written out unchanged rather than expanded here,
    because expanding it would mean this file deciding what the model meant.

    THE BASELINE ROW IS NOT A CREATURE. It is in `ARCHETYPES` so the rarity
    ladder can be read without an archetype's multipliers on top, and it is
    written out because this table is the model and an exception here could hide
    a real archetype going missing. Its Role column says so in words.
    """
    model = _enemy_stats()

    out = []
    for kind in model.ARCHETYPES.values():
        out.append({
            "Name": row_name(kind.name),
            "ArchetypeName": kind.name,
            "Role": kind.role,
            "Cataclysm": kind.cataclysm,
            "HealthShare": _six_places(kind.health_share),
            "DamageShare": _six_places(kind.damage_share),
            "ArmorShare": _six_places(kind.armor_share),
            "AttackIntervalSeconds": _six_places(kind.attack_interval),
            "CritChancePercent": _six_places(kind.crit_chance),
            "CritMultiplierPercent": _six_places(kind.crit_multiplier),
            "MoveSpeedMetresPerSecond": _six_places(kind.move_speed),
            "ChaseSpeedMetresPerSecond": _six_places(kind.chase_speed),
            "EvasionPercent": _six_places(kind.evasion),
            "EnergyShieldFraction": _six_places(kind.energy_shield_fraction),
            "BodyRadiusMetres": _six_places(kind.body_radius),
            "TurnRateDegreesPerSecond": _six_places(kind.turn_rate_degrees),
            "ResistancePercent": _six_places(kind.resistance),
        })

    if not out:
        raise DataError("enemy_stats.ARCHETYPES is empty")
    return unique(out, "Enemy Archetypes")


def enemy_rarities(_book=None) -> list[dict]:
    """One row per rarity: how much of an encounter's score each stat is worth.

    Source: `RARITY_ORDER` and the six AT_COMMON and PER_STEP constants in
    sim/cataclysm_sim/enemy_stats.py.

    RARITY SCALES MAGNITUDE AND NOTHING ELSE. Attack interval, criticals,
    movement and resistance belong to the archetype, so they are not here: a
    Legendary Imp is a bigger Imp, not a slower or tougher kind of creature.
    The model's own header explains why it used to be the other way round.

    WHAT A COLUMN MEANS. `stats_for` computes each of the three as

        score * <stat>PerScore * archetype.<stat>Share

    so the multiplier here is already raised to the power of the rarity's step.
    Written expanded rather than as a base and a growth rate so that reading an
    enemy's stats is a table lookup and a multiply, with no exponent at runtime,
    and so the ladder can be read straight off the file: a Cataclysm Boss has
    about 21 times a Common enemy's health.

    Health is additionally floored at 1 by `stats_for`, and energy shield is a
    fraction of health set by the archetype. Neither is a rarity figure.
    """
    model = _enemy_stats()

    # Read directly rather than through getattr with a default: a missing name
    # must raise here. A check that quietly passes when the thing it reads has
    # been renamed is worse than no check, because it reads as coverage.
    weights = model.scoring.RARITY_WEIGHTS
    if list(weights) != list(model.RARITY_ORDER):
        raise DataError(
            "enemy_stats.RARITY_ORDER is "
            f"{list(model.RARITY_ORDER)} but scoring.RARITY_WEIGHTS, which its "
            f"comment calls the authoritative list, is {list(weights)}. "
            "scoring.py is a port that has drifted from its source twice, so "
            "the two lists must be compared rather than assumed equal.")

    out = []
    for rarity in model.RARITY_ORDER:
        step = model.rarity_step(rarity)
        out.append({
            "Name": row_name(rarity),
            "RarityName": rarity,
            "Step": step,
            "HealthPerScore": _six_places(
                model.HEALTH_AT_COMMON * model.HEALTH_PER_STEP ** step),
            "DamagePerScore": _six_places(
                model.DAMAGE_AT_COMMON * model.DAMAGE_PER_STEP ** step),
            "ArmorPerScore": _six_places(
                model.ARMOR_AT_COMMON * model.ARMOR_PER_STEP ** step),
            "SpawnWeight": _six_places(model.spawn_weight(rarity)),
            "BodyScale": _six_places(
                model.BODY_SCALE_AT_COMMON
                * model.BODY_SCALE_PER_STEP ** step),
        })

    if not out:
        raise DataError("enemy_stats.RARITY_ORDER is empty")

    # THE SPAWN WEIGHTS HAVE TO ADD UP TO ONE FLOOR'S WORTH, because they are
    # shares of a floor's population rather than independent chances. The model
    # reads them out of scoring.DUNGEON_SCORE_MIX, and that file is a port which
    # has drifted from its source twice, so this is checked rather than assumed.
    total = sum(row["SpawnWeight"] for row in out)
    if abs(total - 1.0) > 1e-6:
        raise DataError(
            f"the enemy rarity spawn weights sum to {total}, not 1. They are "
            "how common each rarity is on a floor, which the Dungeon Score "
            "Formula section of docs/Cataclysm_GDD_v2.md states outright, and "
            "shares that do not add up leave part of every floor unfilled.")

    return unique(out, "Enemy Rarities")



# --------------------------------------------------------------------------
# The class passive trees, from the node graphs in docs/
#
# A THIRD SOURCE, BESIDE THE WORKBOOK AND THE PYTHON MODEL. The four class trees
# are authored in C:\Projects\PassiveTreeCreator, a separate tool, and exported
# as JSON into docs/. Those files are the design; this turns them into something
# the game can read, in the same way the workbook sheets are turned into the
# other tables.
#
# WHY THE GAME CANNOT READ THE JSON DIRECTLY. `docs/` is not packaged and is not
# a content directory, so nothing in a built game can open it. Every other piece
# of design data reaches the engine as a DataTable and these do the same.
#
# WHAT IS DELIBERATELY NOT CARRIED ACROSS: the colour of a node, the viewport the
# editor was last looking at, and the free-floating text labels the tool lets an
# author place. All three are about the authoring tool's own canvas rather than
# about the tree, and a screen that drew them would be drawing the editor.
# --------------------------------------------------------------------------

#: The nine class trees that exist: the three War, the three Demonic and, since
#: 2026-11-24, the three Death. The other fifteen are issue #24.
#:
#: EACH IS THE CLASS'S NAME AS THE GAME SPELLS IT, space included.
#: `UCataclysmCharacterCreation::ClassesByDamageType` names the Death classes
#: "Soul Collector", "Necromancer" and "Shadow", and
#: `UCataclysmPassiveTree::TreeIsReachable` compares a node's `Tree` column with
#: that list as written, so the column must say "Soul Collector" and not
#: "SoulCollector". The file name and the row names cannot hold the space; see
#: `tree_file` and `tree_row_prefix`.
CLASS_TREES = ("Berserker", "Bulwark", "Saboteur", "Masochist", "Ravager",
               "Ritualist", "Soul Collector", "Necromancer", "Shadow")

#: How many options a capstone offers, from the design document: "Player chooses
#: one of three options per tier." Fixed at three, which is why the options are
#: three pairs of columns rather than a second table.
CAPSTONE_OPTIONS = 3


def tree_file(name: str) -> pathlib.Path:
    """The tree's file in docs/, with any space in the class name an underscore:
    `docs/Soul_Collector_Class_Tree_Final.json`."""
    return REPO_ROOT / "docs" / f"{name.replace(' ', '_')}_Class_Tree_Final.json"


def tree_row_prefix(name: str) -> str:
    """What every node and edge row of this tree is named with in front.

    THE CLASS NAME WITH ITS SPACES REMOVED, `SoulCollector`, because that is how
    the version 3 save step reads a node's tree back out of its name:
    `ClassOfNode` in `game/Source/Cataclysm/Save/CataclysmSaveRecords.cpp`
    strips the spaces from each class name before it compares, and
    `tools/tests/test_passive_nodes_are_named_for_a_frozen_class.py` holds every
    row to it. For the six one-word classes it is the name itself.
    """
    return name.replace(" ", "")


def _load_tree(name: str) -> dict:
    path = tree_file(name)
    if not path.is_file():
        raise DataError(f"{path.name} is not present")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise DataError(f"{path.name} is not valid JSON: {error}") from error


def _capstone_threshold(node: dict, tree: str) -> int:
    """How many points must be spent in the tree before this capstone opens.

    THREE PLACES TO LOOK, AND ALL THREE ARE IN USE. The Masochist tree writes it
    as `threshold`, the Saboteur tree as `pointThreshold`, and the Berserker and
    Bulwark trees carry both. Every one of the four also states it in the
    description text. Issue #935 asks for them to be made to agree; until then
    anything reading these files has to try all three, and reading only one of
    them would silently give a capstone a threshold of zero.
    """
    data = node["data"]
    for field in ("threshold", "pointThreshold"):
        value = data.get(field)
        if isinstance(value, (int, float)):
            return int(value)

    matched = re.search(r"(\d+)\s+points", data.get("description", ""))
    if not matched:
        raise DataError(
            f"{tree}: capstone {data.get('name')!r} states no point threshold, "
            f"in a threshold field or in its description")
    return int(matched.group(1))



def _no_duplicates(rows: list[dict], table: str) -> list[dict]:
    """Every row name is distinct, or say which one is not.

    NOT `unique`, WHICH SUFFIXES A DUPLICATE AND SAYS NOTHING. That is the right
    answer for a workbook sheet where two rows can legitimately share a display
    name. These keys are built here out of a tree name and a node identifier, so
    a duplicate means this file built the same key twice and the game would hold
    one node where the design has two.
    """
    seen: set[str] = set()
    for row in rows:
        if row["Name"] in seen:
            raise DataError(f"{table}: two rows are both called {row['Name']!r}")
        seen.add(row["Name"])
    return rows


def passive_nodes(_book=None) -> list[dict]:
    """One row per node, across every class tree in `CLASS_TREES`.

    THE ROW KEY IS THE TREE AND THE NODE IDENTIFIER TOGETHER, because node
    identifiers are only unique within a tree. Fourteen are shared by more than
    one of the four -- `capstone_25` is in all of them -- so keying on the node
    identifier alone would silently merge them and the game would hold one tree's
    worth of capstones for four trees.
    """
    out = []
    for tree in CLASS_TREES:
        data = _load_tree(tree)
        for node in data.get("nodes", []):
            body = node.get("data", {})
            kind = node.get("type", "")
            if kind not in ("basic", "keystone", "capstone"):
                raise DataError(f"{tree}: node {node.get('id')} has kind "
                                f"{kind!r}, which is not one of the three")

            position = node.get("position", {})
            options = body.get("options", []) if kind == "capstone" else []

            row = {
                "Name": f"{tree_row_prefix(tree)}_{node['id']}",
                "Tree": tree,
                "NodeId": node["id"],
                "Kind": kind,
                "NodeName": clean(body.get("name", "")),
                "Description": clean(body.get("description", "")),
                "MaxPoints": int(body.get("maxPoints", 0)),
                # ZERO FOR EVERYTHING THAT IS NOT A CAPSTONE, which is not a
                # missing value: a basic node and a keystone open when their
                # edges allow, not at a number of points spent in the tree.
                "Threshold": (_capstone_threshold(node, tree)
                              if kind == "capstone" else 0),
                "PositionX": float(position.get("x", 0.0)),
                "PositionY": float(position.get("y", 0.0)),
            }

            # THE SABOTEUR'S FOUR CAPSTONES HAVE NONE, and its own descriptions
            # say to choose one of three. That is issue #935. Empty columns
            # carry the gap into the game rather than hiding it, and the screen
            # says a capstone with no options cannot be taken.
            for index in range(CAPSTONE_OPTIONS):
                option = options[index] if index < len(options) else {}
                row[f"Option{index + 1}Name"] = clean(option.get("name", ""))
                row[f"Option{index + 1}Description"] = clean(
                    option.get("description", ""))

            out.append(row)

    if not out:
        raise DataError("no class tree nodes were read")
    return _no_duplicates(out, "Passive Nodes")


def passive_edges(_book=None) -> list[dict]:
    """One row per dependency edge, across every class tree in `CLASS_TREES`.

    AN EDGE IS A REQUIREMENT AND NOT A LINE. `requiredPoints` on an edge from A
    to B means B cannot be taken until A holds that many points. The positions
    of the two nodes are in the node table; nothing here is about drawing.
    """
    out = []
    for tree in CLASS_TREES:
        data = _load_tree(tree)
        node_ids = {node["id"] for node in data.get("nodes", [])}

        for edge in data.get("edges", []):
            source = edge.get("source")
            target = edge.get("target")
            if source not in node_ids or target not in node_ids:
                raise DataError(
                    f"{tree}: edge {edge.get('id')} joins {source} to {target} "
                    f"and one of them is not a node in that tree")

            prefix = tree_row_prefix(tree)
            out.append({
                "Name": f"{prefix}_{edge['id']}",
                "Tree": tree,
                "Source": f"{prefix}_{source}",
                "Target": f"{prefix}_{target}",
                "RequiredPoints": int(edge.get("data", {})
                                      .get("requiredPoints", 0)),
            })

    if not out:
        raise DataError("no class tree edges were read")
    return _no_duplicates(out, "Passive Edges")


# --------------------------------------------------------------------------
# The empire upgrade tree, from docs/Empire_Development_Tree_Final.json
#
# THE FOURTH SOURCE, AND THE SAME ROUTE AS THE CLASS TREES. The empire tree is
# authored in the same separate tool and exported into `docs/`; the owner ruled
# on 2026-08-05 (#260) that this file is the tree and that
# `docs/Empire_Skill_Tree_Keystones.md` is commentary on it. The game cannot read
# `docs/`, so the file reaches it as two tables. Issue #2741, slice 1a.
#
# WHAT IS ADDED THAT THE FILE DOES NOT SAY, each a ruling recorded in
# docs/DECISIONS.md (the #2741 slice 1a entry):
#
#   a branch for every node        derived from where the node sits on the
#                                  canvas; the file's own metadata names the
#                                  quadrants, and nothing else in it says
#   a threshold for the four       200 points spent in the capstone's own
#   branch capstones               branch (coordinator, Q2, 2026-11-18); the file
#                                  gives them neither an edge nor a threshold
#   which nodes can be bought      the owner's scope ruling of 2026-11-18: a
#                                  node whose system the game has, and no other
# --------------------------------------------------------------------------

EMPIRE_TREE_FILE = REPO_ROOT / "docs" / "Empire_Development_Tree_Final.json"

#: The four branches, and the quadrant of the canvas each occupies. The file's
#: own metadata says it in words -- "Branches: Architect (NE), Explorer (SE),
#: Treasury (SW), Artisan (NW)" -- and `_empire_branch` reads it from the
#: positions. North is up, which is a NEGATIVE y on the authoring tool's canvas.
EMPIRE_BRANCHES = ("Architect", "Explorer", "Treasury", "Artisan")

#: What the four tier-milestone capstones in the middle are called as a branch.
#: They belong to no quadrant and their thresholds count the whole tree.
EMPIRE_CENTRE = "Centre"

#: When a branch's own capstone opens: this many points spent in that branch.
#: THE FILE GIVES THEM NOTHING. The commentary says they are "not mutually
#: exclusive" and are taken "by investing enough points into each respective
#: branch"; the class trees' last capstone opens at 200 of 230. Ruled by the
#: coordinating session on 2026-11-18 (Q2), a judgement under the owner's
#: delegation of unstated numbers.
EMPIRE_BRANCH_CAPSTONE_THRESHOLD = 200

#: WHAT A REFUSED NODE IS WAITING FOR, in the words a player reads after "it
#: needs". The owner's scope ruling of 2026-11-18, verbatim: "The other nodes are
#: shown but cannot be bought until their system is built, as unbuilt city
#: upgrades are refused today." Each clause names the system and, where there is
#: one, the issue that builds it.
#:
#: GOLD IS A SYSTEM THE GAME HAS SINCE ISSUE #2742 (closed 2026-11-19): kills
#: drop it, the character holds a balance, and a city upgrade costs it. So
#: neither "gold" nor "a gold cost for city upgrades" is a reason any more, and
#: slice 6 (2026-11-20) gave each node those two held the reason it really
#: waits on, or built it. The coordinating session's rulings R1 to R10 of that
#: day; `docs/DECISIONS.md` has them.
EMPIRE_TREE_SYSTEMS = {
    "market": "a market, which the game does not have",
    "gold cap": "a gold cap, which the game does not have",
    "gold cap and death loss": ("a gold cap and a gold loss on death, which the "
                                "game does not have"),
    # #2741's crafting group, G4 (the coordinating session, 2026-11-25): the one "crafting" reason split into what
    # each row still waits on, now that the forge is built. The ten nodes the owner left unbuilt the same day ("Leave
    # them unbuilt for now") name issue #2897, which holds them.
    "crafting station": ("a crafting station with workers, a queue or a project that finishes later, which the "
                         "game does not have (issue #2897)"),
    "double craft": "a double craft, which the game does not have (issue #2897)",
    "exalted item": "an Exalted item, which the game does not have (issue #2897)",
    "item tier": "an item tier, which item bases and their stats do not have (issue #2897)",
    "crafted-affix mark": "a mark on each affix the forge crafted, which the game does not have (issue #2741)",
    "held-over residue": "residue held over between crafts, which the game does not have (issue #2741)",
    "loot chest": "a loot chest on a dungeon floor, which the game does not have",
    "stash": "a stash inside dungeons, which the game does not have",
    "modifier reroll": "dungeon modifier rerolls, which the game does not have",
    "loot filter": "a loot filter, which the game does not have",
    "destroy upgrade": ("a city screen action that destroys a city upgrade, "
                        "which the game does not have"),
}

#: EVERY NODE AND EVERY CENTRE CAPSTONE OPTION THE GAME WILL BUILD IN #2741, by
#: name, with the slice that makes it act: 3 for the city numbers, 4 for a
#: dungeon's walk and depth, 5 for the player's loot and movement, 6 for gold
#: (slice 6a, 2026-11-20: the gold a kill drops and a city upgrade's price), 7
#: for the four nodes that read a Treasurer city upgrade (2026-11-21), 8 for
#: Imperial Prowess's extra option (2026-11-21), 9 for the five the coordinating
#: session ruled under the owner's delegation (2026-11-21; V1 to V19, 2026-11-22).
#: Until its slice lands a node here can be bought and does nothing.
#:
#: TOGETHER WITH `EMPIRE_TREE_REFUSED` IT NAMES EVERY NODE AND OPTION, AND
#: `empire_tree_nodes` REFUSES A NAME IN NEITHER OR IN BOTH. A node added to the
#: tree file therefore stops generation until someone decides which it is,
#: rather than becoming buyable by default.
#:
#: READINGS THAT DECIDE MEMBERSHIP, each a coordinator ruling of 2026-11-18:
#: Veteran's Edge's "Empire XP" is empire upgrade points (C4); "surge damage" is
#: the damage a resolving dungeon does to a city (C4), so Urban Fortification,
#: Civil Defense and Imperial Command are here; Dungeon Mastery's "dungeon type"
#: is the design's four dungeon types (Q7).
EMPIRE_TREE_BUILT = {
    # The four tier-milestone capstones' options.
    "The Aegis of Hope": 3, "The Sentinel": 3, "The Flood Barrier": 3,
    "The Delver": 4, "The Collector's Decree": 4, "The Warlord": 4,
    "The Last Stand": 4,
    "The Hoarder": 5,
    # #47 slice F6: the two crafting options, ruled by the coordinating session
    # on 2026-11-22 (R1 to R8). Numbered 10, the slice after slice 9.
    "The Soul Forge": 10, "The Master Tinkerer": 10,
    # #47 slice F6a: the five forge gold nodes, ruled by the coordinating session
    # on 2026-11-22 (Q1 to Q10; the owner's 30% cap for Architectural Synergy)
    # and 2026-11-24 (A, B). Numbered 11, the slice after F6's 10.
    "Workshop Upgrades": 11, "Apprentice Oversight": 11,
    "Architectural Synergy": 11, "Treasury Synergy": 11, "Forge Upgrades": 11,
    # #47 slice F6b: the three forge material nodes, ruled by the coordinating
    # session on 2026-11-22 (Q11 to Q17) and 2026-11-25 (N1 to N5). Numbered 12,
    # the slice after F6a's 11.
    "Material Conservation": 12, "Resource Management": 12, "Tempting Fate": 12,
    # #2741's crafting group, slices C1 and C2: a forge craft's residue and days,
    # ruled by the coordinating session on 2026-11-25. Numbered 13, the slice
    # after F6b's 12.
    "Tool Maintenance": 13, "Limited Knowledge": 13, "Heat Shielding": 13,
    "Stable Compounds": 13, "Master's Touch": 13, "Master Craftsman": 13,
    "Industrial Speed": 13, "Sovereign's Assembly": 13,
    # #2741's crafting group, slices C3 and C4: a forge craft's affixes, ruled
    # by the coordinating session on 2026-11-25 (D1 to D4). Numbered 14.
    "Precision Crafting": 14, "Master's Workshop": 14, "Quality Materials": 14,
    "Apprentice Smith": 14, "Rough Draft": 14,
    # #2741's crafting group, slice C6: Ancient Techniques and Residue Protocols,
    # ruled by the coordinating session on 2026-11-25 (R1, R2). Numbered 15.
    "Ancient Techniques": 15, "Residue Protocols": 15,
    # Slice 8: the project owner, 2026-11-21, "Imperial prowess lets you pick an
    # additional keystone from a previous tier"; J1 to J5 the same day.
    "Imperial Prowess": 8,
    # Architect.
    "Foundation": 3, "Structural Integrity": 3, "Urban Planning": 3,
    "Fortified Gates": 3, "Enduring Foundations": 3, "Civil Order": 3,
    "Border Patrol": 3, "Emergency Shelters": 3, "Masonry Techniques": 3,
    "Urban Fortification": 3, "Imperial Census": 3, "Civil Defense": 3,
    "Supply Lines": 3, "Reinforced Walls": 3, "The Mason's Guild": 3,
    "Global Vigilance": 3, "Metropolitan Growth": 3, "Siege Resistance": 3,
    "Monument Building": 3, "Strategic Reserve": 3, "Martial Law": 3,
    "Imperial Decree": 3, "Bastion Spirit": 3, "Sovereign's Might": 3,
    "Manifest Destiny": 3, "Unyielding Defense": 3, "Iron Will": 3,
    "Fortified Pillar": 3, "Reclaimer's Resolve": 3, "Imperial Command": 3,
    "Beacon of Hope": 3, "Last Stand Protocols": 3, "The Architect's Legacy": 3,
    "The Royal Engineer's Blessing": 3,
    "Imperial Roads": 4,
    # Slice 9: Civic Pride, each of the four population restores 5% larger a
    # point (V11, 2026-11-22).
    "Civic Pride": 9,
    # Explorer.
    "Pacing": 4, "Fleet Footed": 4, "Overclock": 4, "Temporal Mastery": 4,
    "Sovereign's Haste": 4, "Opportunist": 4, "Tactical Entry": 4,
    "Rapid Descent": 4, "Deep Boring": 4, "Architect of Greed": 4,
    "Exclusionary Mapping": 4, "Infinite Depths": 4, "Architectural Insight": 4,
    "Veteran's Edge": 4,
    # Slice 9: Scout Parties, a bonus dungeon that threatens nothing (V15 to V19,
    # 2026-11-22).
    "Scout Parties": 9,
    "Eyes of the Empire": 5, "Opportunistic Looting": 5, "Pathfinding": 5,
    "Bounty": 5, "Dungeon Mastery": 5, "Efficiency Premium": 5,
    "Quality over Quantity": 5, "Relentless Pursuit": 5, "Dungeon Expertise": 5,
    "Spoils of War": 5, "Temporal Efficiency": 5, "The High Roller": 5,
    "Manifest Wealth": 5, "Speed Runner": 5, "Dungeon Legend": 5,
    "Plunderer's Eye": 5, "One-Day Specialist": 5,
    "The Cartographer's Blessing": 5,
    # Treasury: the one node about people rather than gold.
    "Golden Age": 3,
    # Treasury, slice 6a: the gold a kill drops (G1 to G3) and a city upgrade's
    # price (P1, P2), the coordinating session's rulings of 2026-11-20. Price
    # Controls is 6b's: its P3 reading was withdrawn the same day.
    "Coinage": 6, "Minting Rights": 6, "Untouchable Wealth": 6,
    "Wartime Economy": 6, "Profiteering": 6, "Prosperity Index": 6,
    "Fiscal Policy": 6, "Municipal Bonds": 6, "Public Works": 6,
    "War Funding": 6,
    # Treasury, slice 6b: the run record's five (B1 to B5, 2026-11-20).
    "Price Controls": 6, "Stimulus Package": 6, "Cleanup Crew": 6,
    "Salvage Rights": 6, "Investment Funds": 6,
    # Treasury, slice 6c: the gold held and the day's income (B6 to B8).
    "The Gilded Path": 6, "Venture Capital": 6, "Imperial Reserve": 6,
    "Pax Imperialis": 6, "Wealth Interest": 6,
    # Treasury, slice 6d: the owner's rulings of 2026-11-20 (D8, D9).
    "Treasury Guard": 6, "Tax Collection": 6,
    # Treasury, slice 9: Reinvestment, tier 1 gold spent on city upgrades across
    # runs (V2 to V5, V20, 2026-11-22).
    "Reinvestment": 9,
    # And Economic Bailout, paid automatically when a city's defence reaches 0
    # (V6 to V10, 2026-11-22).
    "Economic Bailout": 9,
    # And Bribery, a map action once a surge (V12 to V14, 2026-11-22).
    "Bribery": 9,
    # Treasury, slice 6e: the gold debt the owner allowed through them (D1 to D7).
    "Global Credit": 6, "Sovereign Loans": 6,
    # Treasury, slice 7: the four nodes that read a Treasurer city upgrade, which
    # #2781 made buyable (R1 to R4; readings T1 to T4, ruled 2026-11-21).
    "Commercial Hubs": 7, "Economic Zones": 7, "Trade Routes": 7,
    "The Golden Touch": 7,
    # The centre: a tier milestone's option, the gold a kill drops.
    "The Midas Touch": 6,
    # The three build nodes, issue #2832: a city upgrade's build days, which issue
    # #2825 gave it, and Rapid Funding's Rush. Ruled by the coordinating session on
    # 2026-11-21 (the nine-node entry of docs/DECISIONS.md).
    "Scaffolding": 6, "Rapid Renovation": 6, "Rapid Funding": 6,
    # Artisan: crafting materials already drop.
    "Scavenger": 5,
    # The two dismantle nodes, issue #2848 slice S2, ruled by the coordinating session on 2026-11-22. Built outside
    # #2741's slices, so the number is the issue's.
    "Salvage Protocol": 2848, "Disassembly Expert": 2848,
}

#: EVERY NODE AND OPTION THAT CANNOT BE BOUGHT YET, by name, with the key of the
#: system it waits for in `EMPIRE_TREE_SYSTEMS`.
#:
#: FIELD DEPOT I, II AND III ARE RANKS, "each replacing the one before", and the
#: file puts them on three separate paths. Ruled 2026-11-18 (Q4): II will require
#: I held and III will require II, beside their own edges -- built with the
#: stash, not now, because none of the three can be bought until then.
EMPIRE_TREE_REFUSED = {
    # The four tier-milestone capstones' options: none waits since #47 slice F6.
    # Architect.
    "Scorched Earth": "destroy upgrade",
    # Explorer.
    "Supply Caches": "loot chest", "Field Depot I": "stash",
    "Field Depot II": "stash", "Field Depot III": "stash",
    "Gamble": "modifier reroll",
    "Auto-Loot": "loot filter",
    # Treasury. Slice 6 built every node its gold rules reach (6a eleven, 6b
    # five, 6c five, 6d two, 6e two, 2026-11-20) and slice 7 the four that read a
    # Treasurer city upgrade (2026-11-21); the rest wait on what their text needs
    # and the game lacks.
    "Black Market": "market", "Master Trader": "market",
    "Merchant Guild": "market", "Thrifty": "market",
    "Surplus Storage": "gold cap", "War Chest": "gold cap and death loss",
    # Artisan.
    "Refining Techniques": "item tier",
    "Explorer Synergy": "double craft",
    "Expanded Station (Jeweler)": "crafting station",
    "Expanded Station (Smith)": "crafting station",
    "Expanded Station (Enchantress)": "crafting station",
    "Flawless Production": "exalted item",
    "Grand Forge": "item tier",
    "Residue Mastery": "exalted item", "The Hidden Stash": "crafting station",
    "Assembly Line": "crafting station",
    "The Alchemist's Transmutation": "crafted-affix mark", "Steady Hands": "held-over residue",
}


def _load_empire_tree() -> dict:
    path = EMPIRE_TREE_FILE
    if not path.is_file():
        raise DataError(f"{path.name} is not present")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise DataError(f"{path.name} is not valid JSON: {error}") from error


def _empire_branch(node: dict) -> str:
    """Which branch a node of the empire tree is in.

    A CAPSTONE THAT IS A DECISION IS IN THE MIDDLE, and the four of them are the
    tier milestones at 25, 50, 100 and 200. Every other node is in the branch
    whose quadrant it sits in. North is a negative y on the authoring tool's
    canvas, which is why the sign is turned over before comparing.

    A NODE EXACTLY ON AN AXIS IS REFUSED rather than given a branch by which
    side of a comparison zero falls on. None is today; the nearest is 33 units
    from one.
    """
    body = node.get("data", {})
    if body.get("kind") == "capstone" and body.get("options"):
        return EMPIRE_CENTRE

    position = node.get("position", {})
    x = float(position.get("x", 0.0))
    north = -float(position.get("y", 0.0))
    if x == 0.0 or north == 0.0:
        raise DataError(
            f"{EMPIRE_TREE_FILE.name}: node {body.get('name')!r} sits on an axis "
            f"of the canvas, so its branch cannot be read from where it is")
    if x > 0.0:
        return "Architect" if north > 0.0 else "Explorer"
    return "Artisan" if north > 0.0 else "Treasury"


def _empire_waits_on(name: str) -> str:
    """What a node or option waits for, or empty when it can be bought.

    A NAME IN NEITHER LIST, OR IN BOTH, STOPS GENERATION. See `EMPIRE_TREE_BUILT`.
    """
    built = name in EMPIRE_TREE_BUILT
    refused = name in EMPIRE_TREE_REFUSED
    if built == refused:
        raise DataError(
            f"{EMPIRE_TREE_FILE.name}: {name!r} is "
            f"{'in both' if built else 'in neither'} of EMPIRE_TREE_BUILT and "
            f"EMPIRE_TREE_REFUSED. Every node and option is one or the other, "
            f"decided by the owner's scope ruling of 2026-11-18 (#2741).")
    if built:
        return ""
    system = EMPIRE_TREE_REFUSED[name]
    if system not in EMPIRE_TREE_SYSTEMS:
        raise DataError(f"{name!r} waits on {system!r}, which EMPIRE_TREE_SYSTEMS "
                        f"does not name")
    return EMPIRE_TREE_SYSTEMS[system]


def empire_tree_nodes(_book=None) -> list[dict]:
    """One row per node of the empire upgrade tree.

    THE ROW NAME IS THE NODE IDENTIFIER, with no tree in front of it, because
    there is one empire tree and its identifiers are unique within it.
    """
    data = _load_empire_tree()
    named: set[str] = set()
    out = []
    for node in data.get("nodes", []):
        body = node.get("data", {})
        kind = node.get("type", "")
        if kind not in ("basic", "keystone", "capstone"):
            raise DataError(f"{EMPIRE_TREE_FILE.name}: node {node.get('id')} has "
                            f"kind {kind!r}, which is not one of the three")

        branch = _empire_branch(node)
        name = clean(body.get("name", ""))
        options = body.get("options", []) if kind == "capstone" else []
        if branch == EMPIRE_CENTRE:
            threshold = int(body.get("threshold") or 0)
            if threshold <= 0:
                raise DataError(f"{EMPIRE_TREE_FILE.name}: the capstone {name!r} "
                                f"states no threshold")
        elif kind == "capstone":
            threshold = EMPIRE_BRANCH_CAPSTONE_THRESHOLD
        else:
            threshold = 0

        # A CENTRE CAPSTONE IS BOUGHT BY TAKING ONE OF ITS OPTIONS, so the
        # options are what is classified and the capstone itself is not. A
        # branch capstone has no options and is classified like any node.
        if branch == EMPIRE_CENTRE:
            waits_on = ""
            named.update(clean(option.get("name", "")) for option in options)
        else:
            waits_on = _empire_waits_on(name)
            named.add(name)

        position = node.get("position", {})
        row = {
            "Name": node["id"],
            "Branch": branch,
            "Kind": kind,
            "NodeName": name,
            "Description": clean(body.get("description", "")),
            "MaxPoints": int(body.get("maxPoints", 0)),
            "Threshold": threshold,
            "PositionX": float(position.get("x", 0.0)),
            "PositionY": float(position.get("y", 0.0)),
            "WaitsOn": waits_on,
        }
        for index in range(CAPSTONE_OPTIONS):
            option = options[index] if index < len(options) else {}
            option_name = clean(option.get("name", ""))
            row[f"Option{index + 1}Name"] = option_name
            row[f"Option{index + 1}Description"] = clean(
                option.get("description", ""))
            row[f"Option{index + 1}WaitsOn"] = (
                _empire_waits_on(option_name)
                if branch == EMPIRE_CENTRE and option_name else "")
        out.append(row)

    if not out:
        raise DataError("no empire tree nodes were read")

    # AND THE OTHER WAY: A NAME IN EITHER LIST THAT IS NO NODE OR OPTION. A node
    # renamed in the tree file would otherwise leave its old name classified and
    # its new one stopped above, and the old one would never be noticed.
    stray = sorted((set(EMPIRE_TREE_BUILT) | set(EMPIRE_TREE_REFUSED)) - named)
    if stray:
        raise DataError(f"EMPIRE_TREE_BUILT or EMPIRE_TREE_REFUSED names "
                        f"{stray}, which no node or option of "
                        f"{EMPIRE_TREE_FILE.name} is called")
    return _no_duplicates(out, "Empire Tree Nodes")


def empire_tree_edges(_book=None) -> list[dict]:
    """One row per dependency edge of the empire upgrade tree.

    THE SAME MEANING AS A CLASS TREE'S EDGE: `requiredPoints` on an edge from A
    to B means B is shut until A holds that many. The three edges chaining the
    four centre capstones require 0 and are carried as they are; what opens a
    capstone is its threshold.
    """
    data = _load_empire_tree()
    node_ids = {node["id"] for node in data.get("nodes", [])}
    out = []
    for edge in data.get("edges", []):
        source = edge.get("source")
        target = edge.get("target")
        if source not in node_ids or target not in node_ids:
            raise DataError(
                f"{EMPIRE_TREE_FILE.name}: edge {edge.get('id')} joins {source} "
                f"to {target} and one of them is not a node")
        out.append({
            "Name": edge["id"],
            "Source": source,
            "Target": target,
            "RequiredPoints": int(edge.get("data", {}).get("requiredPoints", 0)),
        })

    if not out:
        raise DataError("no empire tree edges were read")
    return _no_duplicates(out, "Empire Tree Edges")


#: THE EFFECTS `ECataclysmEmpireTreeEffect` NAMES, in its order, without `None`.
#: HELD HERE AS WELL AS IN C++ ON PURPOSE, and the two have to agree:
#: `tools/tests/test_empire_tree_effects_match_their_texts.py` reads the enum
#: out of `CataclysmDataRows.h` and compares. An effect named in
#: `EMPIRE_TREE_EFFECTS` that is not here stops generation.
EMPIRE_TREE_EFFECT_KINDS = (
    "MaxDefenceAll", "MaxDefenceOutposts", "MaxDefenceBulwarksAndSanctuaries",
    "MaxDefenceSanctuaries", "MaxDefenceNearPillar", "MaxDefencePerDefendedCity",
    "BasePopulationAll", "BasePopulationOutposts", "BasePopulationBulwarks",
    "TotalPopulationCompounded", "TotalPopulationPerBranchStep",
    "DamageTakenCompounded", "PopulationDamageTakenCompounded",
    "DamageTakenAdjacentToPillarCompounded",
    "PopulationDamageTakenNearPillarCompounded",
    "DamageTakenPerActiveTypeCompounded", "DamageTakenByLivingShare",
    "DamageTakenWithUpgrades", "DamageTakenFromCommonestType",
    "ResolveDaysAll", "ResolveDaysOutposts", "ResolveDaysPerBranchStep",
    "ResolveDaysBelowTenPercent",
    "SurgeGapDays",
    # Slice 3b: the rules that act at an event.
    "RetakeFraction", "OutpostDefenceRegeneration",
    "LowestCityRestoresDefence", "LowestCityRestoresPopulation",
    "DamageTakenPerClearThisSurge", "DamageTakenPerCityReclaimed",
    "SanctuarySurvivesAt", "UpgradeEffectAfterDays",
    # Slice 4: a dungeon's walk and depth, and what a clear brings.
    "WalkTimeCompounded", "WalkTimeHalvedAboveFloors", "WalkDaysFewer",
    "WalkDaysFewerWhenAlone", "WalkDaysFewerPerActiveType",
    "WalkDaysFewerOnFallenCities", "WalkDaysFewerPerFloorCleared",
    "WalkOneDayWithinDaysOfFalling", "FloorsAdded", "FloorsAddedPerActiveType",
    "FloorsPerBranchStep", "RemoveAnotherDungeonChance", "EmpirePointsFromClears",
    # Slice 5a: the player's magic find, loot quantity, movement speed and
    # damage, inside a dungeon.
    "MagicFindAdded", "LootQuantityIncreased", "MovementSpeedInDungeons",
    "LootQuantityPerActiveType", "LootQuantityPerTwoDaysRemoved",
    "LootQuantityPerDayRemoved", "MagicFindPerFloorRemoved",
    "MagicFindPerModifierActive", "MagicFindPerFloorCleared",
    "MagicFindPerFiveClears", "LootQuantityPerExplorerCity",
    "MagicFindPerExplorerCity", "LootQuantityPerBranchStep",
    "ExplorerLootDoubledAtOneDay", "DamagePerKindDefeated",
    # Slice 5b: Relentless Pursuit's clears in a row, and the four chances a
    # kill rolls for an extra drop.
    "LootQuantityPerCleanClear", "EliteExtraItemChance", "ExtraMaterialChance",
    "LowTierMaterialExtraChance", "BossLegendaryChancePerTenFloorsAdded",
    # Slice 6a: the gold a kill drops, and a city upgrade's price.
    "GoldIncreased", "GoldMore", "GoldPerStandingCity", "GoldPerActiveType",
    "GoldPerFallenCity", "GoldPerTenThousandCitizens", "GoldPerFiveCityUpgrades",
    "UpgradePriceReduced", "FirstUpgradePriceReduced",
    # Slice 6b: the run record's five.
    "SurgeDamagedCityUpgradePriceReduced", "GoldDoubledBelowHalfPopulation",
    "UnpickedGoldShareAtClear", "GoldPerFloorAtClear", "ClearPayoutIncreased",
    # Slice 6c: the gold held and the day's income.
    "MagicFindPerTenThousandGoldHeld", "MagicFindPerFiftyThousandGoldHeld",
    "DamageTakenPerHundredThousandGold", "GoldPerThousandCitizensDaily",
    "GoldInterestEveryTwentyDays",
    # Slice 6d: Treasury Guard and Tax Collection.
    "DamageTakenPerFiftyThousandGold", "GoldShareOfPopulationEveryTenDays",
    # Slice 6e: Global Credit and Sovereign Loans.
    "UpgradeShortfallAllowed", "UpgradeShortfallAllowedRepaidDaily",
    # Slice 7: Commercial Hubs, Economic Zones, Trade Routes, The Golden Touch.
    "GoldIncreasedOnTreasurerCity", "GoldMoreFromTreasurerCities",
    "GoldEveryTwentyDaysPerTreasurerCity", "GoldAndMaterialsPerTreasurerCity",
    # Slice 8: Imperial Prowess.
    "ExtraEarlierMilestoneOption",
    # The three build nodes, issue #2832.
    "UpgradePriceLess", "UpgradeBuildDaysMultiplied", "UpgradeBuildDaysFewer",
    "UpgradeRushPriceShare", "UpgradeRushDaysFewer",
    # Slice 9: Civic Pride and Reinvestment.
    "PopulationRestoreIncrease", "EmpirePointsPerSpendingStep",
    # Slice 9: Economic Bailout, Bribery and Scout Parties.
    "CityBailout", "DungeonBribe", "BonusDungeonChance",
    # Issue #2848, slice S2: a dismantle's drawn yields.
    "DismantleMaterialsIncreased", "DismantleMaterialsIncreasedSuperbAndAbove",
    # #47 slice F6: The Soul Forge and The Master Tinkerer, read by the forge.
    "UpgradeExtraLevelChance", "EnchantCriticalChance",
    # #47 slice F6a: the five forge gold nodes, read by CraftGoldFactorFor.
    "CraftGoldReduced", "LowTierCraftGoldReduced", "CraftGoldReducedPerBranchStep",
    "CraftGoldReducedPerSpendingStep", "CraftGoldTenthOffChance",
    # #47 slice F6b: the three forge material nodes, drawn by UCataclysmForge::KeepingOn.
    "LowTierMaterialKeptChance", "RareMaterialKeptChance", "HighResidueMaterialsReturnedChance",
    # #2741's crafting group, slices C1 and C2: a forge craft's residue and days.
    "CraftResidueKept", "LowLevelCraftResidueKept", "RareMaterialCraftResidueKept", "StabilizerShareReduced",
    "ResidueResetChancePerHundred", "CraftDaysOff", "CraftDaysReduced", "CraftDaysReducedMore",
    # #2741's crafting group, slices C3 and C4: a forge craft's affixes.
    "AffixRollTopQuarterChance", "AffixRollFloor", "AffixRollMaxedChance", "AffixTierRaisedChance",
    "AffixAddedChance",
    # #2741's crafting group, slice C6.
    "CraftDaysOffOnFullItems", "WornResidueIgnored",
)

#: WHAT EACH BUILT NODE OR OPTION OF THE EMPIRE TREE DOES, by name. Issue
#: #2741 slice 3a: the 31 city nodes and options that act through numbers the
#: run already saves; slice 3b: the seven that act at an event; slice 4 the
#: nineteen of a dungeon's walk and depth; slice 5a the fifteen of the player's
#: stats inside a dungeon; slice 5b Relentless Pursuit and the four drop rules.
#:
#: EACH EFFECT IS (effect, per point) or (effect, per point, step points, step
#: branch, cap). Per point is a fraction for a percentage and days for days; a
#: reduction is the share it takes off, written positive. Every value is the
#: node's own words, and `test_empire_tree_effects_match_their_texts.py` finds
#: each one in its text. SIX OF THE TEN CAPS ARE NOT IN ANY TEXT: Golden
#: Age's and Bastion Spirit's are the owner's ruling 2 of 2026-11-18, "Cap each
#: one", chosen in slice 3a from the simulation (S3, S4); Reclaimer's
#: Resolve's is the coordinating session's Q2 of 2026-11-19, the 15% Global
#: Vigilance's own text states; Architectural Insight's +10 floors is its
#: W4 of 2026-11-19, under ruling 2; and Temporal Efficiency's +50% is its D3
#: of 2026-11-19, under ruling 2, the cap Efficiency Premium's text states;
#: and Relentless Pursuit's +100% is its D5 of 2026-11-19, under ruling Q9,
#: sized as #1390 sized Speed Runner's.
#: Global Vigilance's 15%, Sovereign's Haste's 30, Speed Runner's 100% and
#: Efficiency Premium's 50% are in their texts. Each is recorded as a judgement.
#:
#: READINGS, each a judgement of 2026-11-18 (S5): "Max Health" and "Defense" of
#: a city are its maximum defence; "within 2 rings of the Pillar" is rings 0 to
#: 2 and "adjacent to the Pillar" ring 1; "Surge damage", "damage from
#: dungeons" and "damage from dungeon resolutions" are the damage a dungeon
#: does to its city, at all three places it does it (C4, and the simulation's
#: `damage_taken`); a "defensive upgrade" raises maximum defence or resists
#: defence loss.
EMPIRE_TREE_EFFECTS = {
    # Maximum defence: increases, summed.
    "Foundation": [("MaxDefenceAll", 0.05)],
    "Fortified Gates": [("MaxDefenceOutposts", 0.08)],
    "Masonry Techniques": [("MaxDefenceBulwarksAndSanctuaries", 0.10)],
    "Reinforced Walls": [("MaxDefenceAll", 0.08)],
    "Monument Building": [("MaxDefenceSanctuaries", 0.10)],
    "Beacon of Hope": [("MaxDefenceNearPillar", 0.50)],
    "Imperial Decree": [("MaxDefenceAll", 0.20), ("BasePopulationAll", -0.10)],
    "The Aegis of Hope": [("MaxDefenceOutposts", 0.50)],
    "The Royal Engineer's Blessing": [("MaxDefencePerDefendedCity", 0.02)],
    # Population.
    "Urban Planning": [("BasePopulationOutposts", 0.05)],
    "Imperial Census": [("BasePopulationBulwarks", 0.10)],
    "Metropolitan Growth": [("BasePopulationAll", 0.15)],
    "Manifest Destiny": [("TotalPopulationCompounded", 0.02)],
    "Golden Age": [("TotalPopulationPerBranchStep", 0.01, 20, "Architect", 0.40)],
    # Damage a dungeon does to a city: factors, compounded per point.
    "Structural Integrity": [("DamageTakenCompounded", 0.015)],
    "Urban Fortification": [("DamageTakenCompounded", 0.04)],
    "Civil Defense": [("PopulationDamageTakenCompounded", 0.015)],
    "Supply Lines": [("DamageTakenAdjacentToPillarCompounded", 0.015)],
    "Siege Resistance": [("DamageTakenCompounded", 0.0133)],
    "Sovereign's Might": [("DamageTakenCompounded", 0.01)],
    "Unyielding Defense": [("DamageTakenPerActiveTypeCompounded", 0.005)],
    "Fortified Pillar": [("PopulationDamageTakenNearPillarCompounded", 0.02)],
    "Iron Will": [("DamageTakenByLivingShare", 0.01)],
    "Imperial Command": [("DamageTakenWithUpgrades", 0.40)],
    "The Sentinel": [("DamageTakenFromCommonestType", 0.10)],
    # Days.
    "Border Patrol": [("ResolveDaysOutposts", 1.0)],
    "Emergency Shelters": [("ResolveDaysOutposts", 3.0)],
    "Strategic Reserve": [("ResolveDaysAll", 1.0)],
    "Bastion Spirit": [("ResolveDaysPerBranchStep", 1.0, 20, "Explorer", 15.0)],
    "Martial Law": [("ResolveDaysBelowTenPercent", 5.0)],
    "The Flood Barrier": [("SurgeGapDays", 20.0)],
    # Slice 3b: the rules that act at an event, each reading a judgement of the
    # coordinating session's Q1 to Q7 of 2026-11-19 (`docs/DECISIONS.md`).
    "The Architect's Legacy": [("RetakeFraction", 0.75)],
    "Civil Order": [("OutpostDefenceRegeneration", 0.03)],
    "The Mason's Guild": [("LowestCityRestoresDefence", 0.20),
                          ("LowestCityRestoresPopulation", 0.15)],
    "Global Vigilance": [("DamageTakenPerClearThisSurge", 0.01, 0, "", 0.15)],
    "Reclaimer's Resolve": [("DamageTakenPerCityReclaimed", 0.01, 0, "", 0.15)],
    "Last Stand Protocols": [("SanctuarySurvivesAt", 1.0)],
    "Enduring Foundations": [("UpgradeEffectAfterDays", 0.02)],
    # Slice 4: a dungeon's walk and depth, each reading a judgement of the
    # coordinating session's W1 to W9 of 2026-11-19 (`docs/DECISIONS.md`).
    # The walk: a share off compounded per point; flat days off; Tactical
    # Entry's threshold in floors; The Last Stand option's days.
    "Pacing": [("WalkTimeCompounded", 0.025)],
    "Overclock": [("WalkTimeCompounded", 0.025)],
    "Temporal Mastery": [("WalkTimeCompounded", 0.025)],
    "Fleet Footed": [("WalkTimeCompounded", 0.12)],
    "Tactical Entry": [("WalkTimeHalvedAboveFloors", 50.0)],
    "The Delver": [("WalkDaysFewer", 5.0)],
    "Opportunist": [("WalkDaysFewerWhenAlone", 1.0)],
    "Sovereign's Haste": [("WalkDaysFewerPerActiveType", 1.0, 0, "", 30.0)],
    "Imperial Roads": [("WalkDaysFewerOnFallenCities", 1.0)],
    "Rapid Descent": [("WalkDaysFewerPerFloorCleared", 0.1)],
    "The Last Stand": [("WalkOneDayWithinDaysOfFalling", 7.0)],
    # The depth: floors per point, added when a dungeon is made (W3).
    "Deep Boring": [("FloorsAdded", 1.0)],
    "Architect of Greed": [("FloorsAdded", 1.0)],
    "Exclusionary Mapping": [("FloorsAdded", -1.0)],
    "The Collector's Decree": [("FloorsAdded", 10.0)],
    "Infinite Depths": [("FloorsAddedPerActiveType", 2.0)],
    "Architectural Insight": [("FloorsPerBranchStep", 1.0, 10, "Architect", 10.0)],
    # A clear.
    "The Warlord": [("RemoveAnotherDungeonChance", 0.15)],
    # #47 slice F6: the forge. "15% chance to increase its level by 2 instead of
    # 1"; "5% chance to hit a critical success" (docs/Empire_Skill_Tree_Keystones.md).
    "The Soul Forge": [("UpgradeExtraLevelChance", 0.15)],
    "The Master Tinkerer": [("EnchantCriticalChance", 0.05)],
    # #47 slice F6a: the forge's gold. "-1.5% Gold cost for all crafting actions
    # per point"; "Reduces the Gold cost of all Tier 1 and Tier 2 crafts by 20%"
    # (a craft's tier is its material's, Q3); "-1% crafting Gold cost per 10
    # points invested in the Treasury branch per point", capped at 30% by the
    # owner (2026-11-22, Q4); "For every 50,000 Gold spent on crafting, gain a
    # permanent 1% discount ... (Cap 30%)" (the count is the game's, Q5); "1%
    # chance per point to reduce the cost of a craft in the forge by 10%".
    "Workshop Upgrades": [("CraftGoldReduced", 0.015)],
    "Apprentice Oversight": [("LowTierCraftGoldReduced", 0.20)],
    "Architectural Synergy": [("CraftGoldReducedPerBranchStep", 0.01, 10, "Treasury", 0.30)],
    "Treasury Synergy": [("CraftGoldReducedPerSpendingStep", 0.01, 0, "", 0.30)],
    "Forge Upgrades": [("CraftGoldTenthOffChance", 0.01)],
    # #47 slice F6b: the forge's materials. "2% chance per point to not consume
    # T2 and below crafting materials"; "+1% chance per point to not consume
    # Rare materials when crafting" ("Rare" is tier 3 and above, Q14); "While
    # your item's CR is above 500, all crafting actions on that item have a 15%
    # chance to return all crafting materials used" (the 500 is
    # UCataclysmForge::TemptingFateResidueAbove, read before the craft, Q15, N5).
    "Material Conservation": [("LowTierMaterialKeptChance", 0.02)],
    "Resource Management": [("RareMaterialKeptChance", 0.01)],
    "Tempting Fate": [("HighResidueMaterialsReturnedChance", 0.15)],
    # #2741's crafting group, slices C1 and C2 (2026-11-25). "-5% CR gained from
    # all crafting actions per point (multiplicative)"; "-2.5% CR gained from
    # crafting actions on items below +6 per point (multiplicative)"; "-2.5% CR
    # gained from crafting actions using T3+ materials per point
    # (multiplicative)"; "Chaos Stabilizer reduces CR gain by an additional 2.5%
    # per point"; "1% chance per 100 CR on an item to reset CR to 0 when
    # crafting"; "-1 Day to crafting time per point"; "-10% of Days required for
    # any crafting project per point" (the keystones document's figure, ruled
    # by the coordinating session on 2026-11-25 under the owner's delegation:
    # the tree file's 1% rounded away to nothing; 50% at 5 points); "Reduce
    # total crafting time required by 50%".
    "Tool Maintenance": [("CraftResidueKept", 0.05)],
    "Limited Knowledge": [("LowLevelCraftResidueKept", 0.025)],
    "Heat Shielding": [("RareMaterialCraftResidueKept", 0.025)],
    "Stable Compounds": [("StabilizerShareReduced", 0.025)],
    "Master's Touch": [("ResidueResetChancePerHundred", 0.01)],
    "Master Craftsman": [("CraftDaysOff", 1.0)],
    "Industrial Speed": [("CraftDaysReduced", 0.10)],
    "Sovereign's Assembly": [("CraftDaysReducedMore", 0.5)],
    # #2741's crafting group, slices C3 and C4 (2026-11-25). "+3% chance per
    # point for affixes to roll in the top 25% of their range"; "+2% to the
    # minimum roll range of all affixes on a crafted item per point"; "+5%
    # chance per point for a craft to roll maximum values on one random affix"
    # (ruled on 2026-11-25 under the owner's delegation: 50% at 10 points, so
    # the Primal Spark's craft still matters); "1% chance per point to upgrade a random affix
    # to next tier when using the smith"; "+5% chance per point for a crafted
    # item to roll one additional affix".
    "Precision Crafting": [("AffixRollTopQuarterChance", 0.03)],
    "Master's Workshop": [("AffixRollFloor", 0.02)],
    "Quality Materials": [("AffixRollMaxedChance", 0.05)],
    "Apprentice Smith": [("AffixTierRaisedChance", 0.01)],
    "Rough Draft": [("AffixAddedChance", 0.05)],
    # #2741's crafting group, slice C6 (2026-11-25). "-1 Day crafting time per
    # point for crafts with 4 or more affixes" (the owner's question 1, ruled:
    # 4, a full item); "Ignore 5% of Worn Residue per point when it is measured
    # against the Consumption Threshold" (question 3, ruled: the keystones
    # document's figure, as the design document and the simulation use it).
    "Ancient Techniques": [("CraftDaysOffOnFullItems", 1.0)],
    "Residue Protocols": [("WornResidueIgnored", 0.05)],
    "Veteran's Edge": [("EmpirePointsFromClears", 0.03)],
    # Slice 5a: the player inside a dungeon, each reading a judgement of the
    # coordinating session's D1 to D4 of 2026-11-19 (`docs/DECISIONS.md`).
    # Magic find is a flat number of percentage points, because its base is
    # nought; loot quantity, movement speed and damage are increases.
    "Eyes of the Empire": [("MagicFindAdded", 0.05)],
    "Bounty": [("LootQuantityIncreased", 0.05)],
    "The Hoarder": [("MagicFindAdded", 0.25), ("LootQuantityIncreased", 1.00)],
    "Pathfinding": [("MovementSpeedInDungeons", 0.02)],
    "Spoils of War": [("LootQuantityPerActiveType", 0.02)],
    # Days removed from the walk: whole steps of two days, and whole days (C3).
    "Speed Runner": [("LootQuantityPerTwoDaysRemoved", 0.05, 0, "", 1.00)],
    "Efficiency Premium": [("LootQuantityPerDayRemoved", 0.05, 0, "", 0.50)],
    "Quality over Quantity": [("MagicFindPerFloorRemoved", 0.02)],
    "Dungeon Expertise": [("MagicFindPerModifierActive", 0.04)],
    "Manifest Wealth": [("MagicFindPerFloorCleared", 0.01)],
    "Dungeon Legend": [("MagicFindPerFiveClears", 0.01)],
    "The Cartographer's Blessing": [("LootQuantityPerExplorerCity", 0.05),
                                    ("MagicFindPerExplorerCity", 0.05)],
    "Temporal Efficiency": [("LootQuantityPerBranchStep", 0.10, 5, "Artisan", 0.50)],
    # The walk of one day it asks for; the doubling is in C++.
    "One-Day Specialist": [("ExplorerLootDoubledAtOneDay", 1.0)],
    "Dungeon Mastery": [("DamagePerKindDefeated", 0.03)],
    # Slice 5b, each reading a judgement of the coordinating session's D5, D6
    # and E1 to E4 of 2026-11-19 (`docs/DECISIONS.md`). Relentless Pursuit
    # counts the clears since the last death, to a cap (D5); the four drop
    # rules are chances a kill rolls, each held at 100% in C++.
    "Relentless Pursuit": [("LootQuantityPerCleanClear", 0.05, 0, "", 1.00)],
    "Opportunistic Looting": [("EliteExtraItemChance", 0.03)],
    "Plunderer's Eye": [("ExtraMaterialChance", 0.02)],
    "Scavenger": [("LowTierMaterialExtraChance", 0.05)],
    # Per whole 10 floors added to the dungeon; the step is in C++.
    "The High Roller": [("BossLegendaryChancePerTenFloorsAdded", 0.05)],
    # Slice 6a, the coordinating session's rulings of 2026-11-20. THE GOLD A KILL
    # DROPS (G1, the design's pipeline): every "+X% Gold" adds into one increase
    # and The Midas Touch's "50% more" is its own multiplier; each reaches the
    # gold its own words name (G2), and in 6a that is a kill's. The counts (G3)
    # are worked out in C++: cities not fallen, fallen cities not erased, the
    # active Cataclysm types, each whole 10,000 citizens and each whole 5
    # upgrades held by standing cities.
    "Coinage": [("GoldIncreased", 0.05)],
    "Minting Rights": [("GoldIncreased", 0.03)],
    "The Midas Touch": [("GoldMore", 0.50)],
    "Untouchable Wealth": [("GoldPerStandingCity", 0.02)],
    "Wartime Economy": [("GoldPerActiveType", 0.10)],
    "Profiteering": [("GoldPerFallenCity", 0.05)],
    "Prosperity Index": [("GoldPerTenThousandCitizens", 0.01)],
    "Fiscal Policy": [("GoldPerFiveCityUpgrades", 0.03)],
    # A CITY UPGRADE'S PRICE (P1, P2): the reductions add, and C++ holds their
    # sum at 75%; War Funding's is for a city holding no upgrade.
    "Municipal Bonds": [("UpgradePriceReduced", 0.03)],
    "Public Works": [("UpgradePriceReduced", 0.20)],
    "War Funding": [("FirstUpgradePriceReduced", 0.02)],
    # Slice 6b, the coordinating session's B1 to B5 of 2026-11-20. PRICE CONTROLS
    # reads a city's saved mark of Surge damage (B1); STIMULUS PACKAGE doubles a
    # kill's gold while the run's population has fallen below half (B2, its own
    # "more"); CLEANUP CREW pays a tenth of the walk's gold left lying at a clear
    # (B3); SALVAGE RIGHTS pays 50 tier 1 gold a point for every floor at a clear,
    # scaled to the tier (B4, R10); INVESTMENT FUNDS raises that payout (B5).
    "Price Controls": [("SurgeDamagedCityUpgradePriceReduced", 0.04)],
    "Stimulus Package": [("GoldDoubledBelowHalfPopulation", 1.0)],
    "Cleanup Crew": [("UnpickedGoldShareAtClear", 0.10)],
    "Salvage Rights": [("GoldPerFloorAtClear", 50.0)],
    "Investment Funds": [("ClearPayoutIncreased", 0.10)],
    # Slice 6c, the coordinating session's B6 to B8 of 2026-11-20, every gold
    # figure a tier 1 one scaled to the tier (R10). THE GILDED PATH and VENTURE
    # CAPITAL add magic find to a kill for each whole 10,000 and 50,000 gold held
    # (B6), The Gilded Path uncapped because its text names no cap; IMPERIAL
    # RESERVE reduces every city's damage for each whole 100,000, compounded (B7);
    # PAX IMPERIALIS pays every day and WEALTH INTEREST every 20th (B8). The steps
    # are in C++.
    "The Gilded Path": [("MagicFindPerTenThousandGoldHeld", 0.01)],
    "Venture Capital": [("MagicFindPerFiftyThousandGoldHeld", 0.01, 0, "", 0.20)],
    "Imperial Reserve": [("DamageTakenPerHundredThousandGold", 0.01, 0, "", 0.25)],
    "Pax Imperialis": [("GoldPerThousandCitizensDaily", 5.0)],
    "Wealth Interest": [("GoldInterestEveryTwentyDays", 0.005, 0, "", 5000.0)],
    # Slice 6d, the owner's rulings of 2026-11-20, relayed by the coordinating
    # session. TREASURY GUARD reduces every city's damage ("1. to your cities")
    # for each whole 50,000 tier 1 gold held, compounded, held at its own 20% and
    # multiplied with Imperial Reserve (D9); TAX COLLECTION pays 2% a point of the
    # standing cities' citizens as tier 1 gold every 10th day (D8). The steps are
    # in C++.
    "Treasury Guard": [("DamageTakenPerFiftyThousandGold", 0.01, 0, "", 0.20)],
    "Tax Collection": [("GoldShareOfPopulationEveryTenDays", 0.02)],
    # Slice 6e, the owner's ruling of 2026-11-20, "no gold debt unless there's an
    # option for it in the empire tree": GLOBAL CREDIT lets a city upgrade be
    # short by 5% a point of its price and SOVEREIGN LOANS by 20% a point, one
    # allowance and one debt, the whole balance taken and the rest owed, nothing
    # more on credit until it is paid (D2); gold picked up pays it down first
    # (D3), and with Sovereign Loans a tenth of it a day (D4). No cap: built as
    # written, tuned later.
    "Global Credit": [("UpgradeShortfallAllowed", 0.05)],
    "Sovereign Loans": [("UpgradeShortfallAllowedRepaidDaily", 0.20)],
    # Slice 7, the four nodes that read a Treasurer city: a standing city holding
    # at least one upgrade of the City Upgrades sheet's Treasurer branch (T1,
    # ruled 2026-11-21). COMMERCIAL HUBS increases a kill's gold while the dungeon's city
    # is one; ECONOMIC ZONES is a "more" on the gold such a city generates (T2);
    # TRADE ROUTES pays 50 tier 1 gold a point for each every 20th day (T3); THE
    # GOLDEN TOUCH increases a kill's gold and crafting materials for each (T3,
    # T4). The figures are the texts'; the counts and the steps are in C++.
    "Commercial Hubs": [("GoldIncreasedOnTreasurerCity", 0.20)],
    "Economic Zones": [("GoldMoreFromTreasurerCities", 0.05)],
    "Trade Routes": [("GoldEveryTwentyDaysPerTreasurerCity", 50.0)],
    "The Golden Touch": [("GoldAndMaterialsPerTreasurerCity", 0.10)],
    # Slice 8, the project owner's ruling of 2026-11-21: "Keystones are tier gated
    # behind point thresholds. Imperial prowess lets you pick an additional
    # keystone from a previous tier." The keystones with point thresholds are the
    # centre milestones' options; "an additional keystone" is one (J2). The rules
    # that take it are in C++ (`UCataclysmEmpireTree::ChooseExtraOption`).
    "Imperial Prowess": [("ExtraEarlierMilestoneOption", 1.0)],
    # The three build nodes, issue #2832, ruled by the coordinating session on
    # 2026-11-21. SCAFFOLDING halves a city upgrade's price, a multiplier of its
    # own after the reductions are held at their cap ("less" multiplies, as in the
    # damage pipeline), and doubles its days; RAPID RENOVATION takes a day off a
    # point; RAPID FUNDING offers a Rush at 150% of the price after every discount
    # for 2 days fewer. The doubling comes first, then the days off, then the
    # floor of 1 day: `UCataclysmEmpireTree::UpgradeBuildDaysFor`.
    "Scaffolding": [("UpgradePriceLess", 0.50), ("UpgradeBuildDaysMultiplied", 2.0)],
    "Rapid Renovation": [("UpgradeBuildDaysFewer", 1.0)],
    "Rapid Funding": [("UpgradeRushPriceShare", 1.50), ("UpgradeRushDaysFewer", 2.0)],
    # Slice 9, the coordinating session's rulings of 2026-11-21 under the owner's
    # delegation and V1 to V19 of 2026-11-22. CIVIC PRIDE: the design has no
    # population growth, so "growth" is population coming back, and each of the
    # four population restores is 5% larger a point (V11); C++ holds each at the
    # city's maximum.
    "Civic Pride": [("PopulationRestoreIncrease", 0.05)],
    # REINVESTMENT: 1% more empire points a clear pays for each 25,000 tier 1 gold
    # spent on city upgrades across runs (V2, V4, V5, V20), capped at 30% on its
    # own and added to Veteran's Edge's (V3). The 30% is the coordinating
    # session's ruling of 2026-11-21: Veteran's Edge at its full 10 points. The
    # 25,000 step and the count are in C++.
    "Reinvestment": [("EmpirePointsPerSpendingStep", 0.01, 0, "", 0.30)],
    # ECONOMIC BAILOUT: a city whose defence reaches 0 is saved, once per city per
    # run, when the balance pays 50,000, 100,000 or 150,000 tier 1 gold for an
    # Outpost, a Bulwark or a Sanctuary; never the Pillar; it keeps half its
    # maximum defence (the ruling of 2026-11-21, V6 to V10). The fees and the rules
    # are in C++; the row says only that it is held.
    "Economic Bailout": [("CityBailout", 1.0)],
    # BRIBERY: once a surge, the player pays 20,000 tier 1 gold to reset one
    # resolving dungeon's timer to its start (the ruling of 2026-11-21, V12 to
    # V14). The fee and the rules are in C++ (`FCataclysmDungeonBribe`).
    "Bribery": [("DungeonBribe", 1.0)],
    # SCOUT PARTIES: 10% a point that a clear discovers a bonus dungeon on the same
    # city, always Basic, which threatens nothing and pays as any dungeon does
    # (V15 to V19). The rules are in C++ (`UCataclysmEmpireRun::AddBonusDungeon`).
    "Scout Parties": [("BonusDungeonChance", 0.10)],
    # Issue #2848, slice S2, ruled 2026-11-22. SALVAGE PROTOCOL: "Dismantling items now yields 25% more crafting
    # materials" at its 10 points, so 2.5% a point (N6). DISASSEMBLY EXPERT: "+20% crafting materials returned when
    # dismantling Rare and above items", Rare read as Superb (O4); C++ applies it from Superb up. Both raise the drawn
    # yields, fragments, Dust and base stones, and neither raises the refund of stones spent on upgrades.
    "Salvage Protocol": [("DismantleMaterialsIncreased", 0.025)],
    "Disassembly Expert": [("DismantleMaterialsIncreasedSuperbAndAbove", 0.20)],
}


def empire_tree_effects(_book=None) -> list[dict]:
    """One row per effect of a built node or option of the empire upgrade tree.

    THE ROW NAME IS THE NODE IDENTIFIER WITH `#1`, `#2` AFTER IT, and nothing
    reads it: which node is the `Node` column, as on the class trees' grant
    table. An option's row carries its capstone's identifier and the option's
    number, 1 to 3.
    """
    data = _load_empire_tree()
    where: dict[str, tuple[str, int]] = {}
    for node in data.get("nodes", []):
        body = node.get("data", {})
        name = clean(body.get("name", ""))
        if _empire_branch(node) == EMPIRE_CENTRE:
            for index, option in enumerate(body.get("options", [])):
                where[clean(option.get("name", ""))] = (node["id"], index + 1)
        else:
            where[name] = (node["id"], 0)

    out = []
    counts: dict[str, int] = {}
    for name, effects in EMPIRE_TREE_EFFECTS.items():
        if name not in where:
            raise DataError(f"EMPIRE_TREE_EFFECTS names {name!r}, which no node "
                            f"or option of {EMPIRE_TREE_FILE.name} is called")
        if name not in EMPIRE_TREE_BUILT:
            raise DataError(f"EMPIRE_TREE_EFFECTS gives {name!r} an effect, and "
                            f"it is not in EMPIRE_TREE_BUILT: a node that cannot "
                            f"be bought has nothing to do")
        node_id, option = where[name]
        for effect in effects:
            kind, per_point = effect[0], effect[1]
            step_points, step_branch, cap = (effect[2:] + (0, "", 0.0))[:3]
            if kind not in EMPIRE_TREE_EFFECT_KINDS:
                raise DataError(f"{name!r} has the effect {kind!r}, which "
                                f"EMPIRE_TREE_EFFECT_KINDS does not name")
            if step_points and step_branch not in EMPIRE_BRANCHES:
                raise DataError(f"{name!r} counts steps of {step_branch!r}, "
                                f"which is not one of {EMPIRE_BRANCHES}")
            counts[node_id] = counts.get(node_id, 0) + 1
            out.append({
                "Name": f"{node_id}#{counts[node_id]}",
                "Node": node_id,
                "Option": option,
                "Effect": kind,
                "PerPoint": float(per_point),
                "StepPoints": int(step_points),
                "StepBranch": step_branch,
                "Cap": float(cap),
            })

    if not out:
        raise DataError("no empire tree effects were written")
    return _no_duplicates(out, "Empire Tree Effects")


#: The states of the character a passive bonus may be made to depend on.
#:
#: HELD HERE AS WELL AS IN C++ ON PURPOSE, and the two have to agree.
#: `UCataclysmPassiveTree::AccumulateInto` turns each of these names into an
#: `ECataclysmStatCondition`, and a name it does not recognise is applied with no
#: condition -- a bonus that holds all the time instead of some of the time,
#: silently and in the player's favour. Refusing an unknown name when the file is
#: written is the only place that can be caught. Issue #959.
#:
#: EACH CARRIES ITS OWN UNITS AND ITS OWN RANGE, since issue #962 added a window
#: measured in seconds beside a threshold measured in percent. One shared range of
#: 0 to 100 would have refused a window longer than a minute and, worse, accepted
#: a percentage written where seconds belong: `seconds_after_health_cost` with 35
#: is a thirty-five second window, which is not what anybody meant and which
#: nothing at run time would report.
#:
#: The value is (lowest, highest, what the number means), or `None` for a
#: condition that compares nothing and therefore takes no value at all.
#:
#: A CONDITION WITH NO NUMBER IS A REAL SHAPE, since issue #962. Four of these
#: compare a reading against a threshold; `while_bleeding` names a kind of effect
#: and the kind is in the name. There is no column that could hold it -- the
#: value column is a float and a tag name is not a number -- so the name carries
#: it, and a value typed beside it is refused rather than quietly ignored. A
#: range of (0, 0) would have accepted a stray zero and read as a threshold of
#: nothing, which is why this is `None` and not a degenerate range.
#: How many options a capstone node offers. Issue #1029.
#:
#: HELD HERE AS WELL AS IN C++ AND THE TWO HAVE TO AGREE.
#: `UCataclysmPassiveTree::CapstoneOptions` is the other copy, and the passive
#: node sheet's own shape is a third statement of it: it carries `Option1Name`
#: through `Option3Name` and no fourth.
#: `test_the_capstone_option_count_matches_the_engine` holds them together.
CAPSTONE_OPTIONS = 3

CONDITIONS = {
    # "While at or below 20% health" is `health_at_or_below` with 20.
    "health_at_or_below": (0.0, 100.0, "a percentage of maximum health"),

    # "While below 20% health" is `health_below` with 20. Issue #1051.
    #
    # A SECOND HEALTH THRESHOLD BECAUSE THE TWO REALLY DIFFER, at exactly the
    # threshold. Seven nodes across the four trees say "at or below" and take
    # the predicate above; The Final Vow's first option, The Last Drop, is the
    # only one in the game that states a health threshold as a STATE and words
    # it "below". `skill_health_cost_above` is the precedent: it exists because
    # "above" genuinely differs from "at or below", and its comment records a
    # character sitting exactly on that threshold correctly getting nothing.
    #
    # NOT SOLVED BY REWORDING THE NODE. The project owner rewrote all twelve
    # Masochist capstone options on 2026-08-27 under issue #1031 and this is
    # their text. `CONDITION_WORDS` in the test beside this file is what refuses
    # a row whose predicate and whose sentence disagree, and widening that entry
    # to accept both wordings would have let a node saying "below" be delivered
    # as "at or below", which is the drift the check exists to catch.
    #
    # "DROPPING BELOW" IS NEITHER OF THESE. That is an EVENT, which The Breaking
    # Point and Rock Bottom both state, and an event is not a condition on a
    # modifier at all: `UCataclysmDamageConversion` holds its threshold as a
    # constant and says why.
    "health_below": (0.0, 100.0, "a percentage of maximum health"),

    # "while you are above 50% health" is `health_above` with 50. Issue #1070.
    #
    # THE THIRD HEALTH PREDICATE AND THE FIRST THAT POINTS UPWARDS. The two
    # above both ask whether health has fallen far enough; Ceaseless Penance is
    # the only node in the game that asks whether it is still high, and it is a
    # STATE rather than an event: a character that heals back above the
    # threshold is holding its debuffs again, and one that falls below it is not.
    #
    # NOT `health_at_or_below` READ BACKWARDS. Strictly above 50 and at or below
    # 50 are complements, so the two cover every character between them, but a
    # row carries one predicate and the stat pipeline has no "not". A node
    # wanting "above" needs a predicate that says so.
    #
    # STRICTLY ABOVE, BECAUSE THE OPTION WRITES "above". The same boundary
    # `skill_health_cost_above` draws and for the same reason: a character
    # sitting exactly on half health is not above it, so its debuffs expire.
    "health_above": (0.0, 100.0, "a percentage of maximum health"),

    # "while you are at or above 50% health" is `health_at_or_above` with 50.
    # Issues #1653 and #41.
    #
    # THE FOURTH HEALTH PREDICATE, AND THE SECOND PAIR. `health_at_or_below` and
    # `health_below` differ only in whether a character sitting exactly on the
    # number is included; this and `health_above` are the same pair pointing the
    # other way. Four predicates, two thresholds, each read two ways.
    #
    # WHAT ASKS FOR IT IS A ROW THAT COULD NOT BE WRITTEN WITHOUT IT. "Your
    # ultimate ability cannot be used unless you are below 50% HP" locks the
    # skill when health is AT OR ABOVE 50, and `health_above` is strictly above,
    # so a character on exactly half health could use a skill the sentence
    # forbids. The pipeline has no "not", so `health_below` cannot be read
    # backwards to cover it.
    #
    # ZERO MEANS ALWAYS AND A HUNDRED MEANS ONLY AT FULL HEALTH, which is the
    # mirror of what the endpoints mean for `health_at_or_below`. Both are
    # legitimate rather than data errors, the same judgement the three above
    # carry.
    "health_at_or_above": (0.0, 100.0, "a percentage of maximum health"),

    # "for 2 seconds after you pay a health cost" is
    # `seconds_after_health_cost` with 2. The upper bound is a sanity limit
    # rather than a design rule: the design's longest window is 5 seconds, and
    # anything past a minute is far likelier to be a percentage in the wrong
    # column than a deliberate window.
    "seconds_after_health_cost": (0.0, 60.0, "a number of seconds"),

    # "for 5 seconds after you take damage of a Cataclysm type other than
    # Demonic" is `seconds_after_foreign_damage` with 5. Same units and
    # same bound as the window above; a different event opens it.
    "seconds_after_foreign_damage": (0.0, 60.0, "a number of seconds"),

    # "After using a charge skill gain 20%-40% increased attack speed for 4
    # seconds" is `seconds_after_charge_skill` with 4. Issue #1826.
    #
    # THE FIRST THREE WINDOWS AN ENCHANTMENT OPENS rather than a passive node,
    # and they arrive together because three authored enchantment sentences ask
    # for them. Same units and same bound as the two windows above; a different
    # event opens each.
    #
    # ONE NAME PER EVENT, which is the rule the enumeration states: a general
    # timer would have to carry which event it means, and the row has nowhere
    # to put that, because the value is already the window's length.
    "seconds_after_charge_skill": (0.0, 60.0, "a number of seconds"),

    # "Gain 5%-10% attack speed on basic attack for 4 seconds" is
    # `seconds_after_basic_attack` with 4. Issue #1826.
    #
    # THE BASIC ATTACK IS NOT A ROW OF WeaponSkills.csv, so there is no authored
    # tag naming it and the engine reads the ability slot instead. A search of
    # that sheet for `Slot.Basic` finds nothing and that is correct authoring,
    # not a gap.
    "seconds_after_basic_attack": (0.0, 60.0, "a number of seconds"),

    # "Blocking an attack grants 10%-20% increased damage for 3 seconds" is
    # `seconds_after_block` with 3. Issue #1826.
    #
    # NOT THE SAME QUESTION AS `seconds_after_foreign_damage`, though both open
    # on a blow the character took. That one asks about the Cataclysm type and
    # ignores whether it was blocked; this asks whether it was blocked and
    # ignores the type.
    "seconds_after_block": (0.0, 60.0, "a number of seconds"),

    # "Summoning a minion grants you 5%-10% increased damage for 5 seconds" is
    # `seconds_after_summon` with 5. Issue #1815.
    #
    # `Type.Summon` AND NOT `Keyword.Summon`, per the ruling in #1824. Five
    # weapon skills carry the keyword and only two create a creature; the other
    # three command creatures that already exist.
    "seconds_after_summon": (0.0, 60.0, "a number of seconds"),

    # "When you dodge an attack gain 15%-30% increased damage for 3 seconds" is
    # `seconds_after_dodge` with 3. Issue #1815.
    #
    # THE ROW SAYS "dodge" AND THE ENGINE CALLS IT AN EVADE. The name follows
    # the sentence, as every name here does; the enumerator follows the code it
    # reads. `Outcome.bEvaded` is the reading.
    "seconds_after_dodge": (0.0, 60.0, "a number of seconds"),

    # "Taking a hit reduces your damage by 5%-10% for 3 seconds" is
    # `seconds_after_hit_taken` with 3. Issue #1815.
    #
    # THE ONLY ONE OF THESE THAT OVERLAPS ITS NEIGHBOURS. A blocked hit and an
    # evaded hit are both hits, so this opens together with `seconds_after_block`
    # and `seconds_after_dodge` rather than instead of them. It is also not the
    # same question as `seconds_after_foreign_damage`, which asks about the
    # Cataclysm type and is reached only when something actually got through.
    "seconds_after_hit_taken": (0.0, 60.0, "a number of seconds"),

    # "When your class resource is full, gain 10%-20% damage reduction for 3
    # seconds" is `seconds_after_resource_full` with 3. Issue #1815.
    #
    # AN EVENT, AND `class_resource_at_maximum` IS THE STATE. The two read the
    # same pool and answer different questions: that one holds for as long as
    # the bar is full, this one opens at the moment it fills and then ages while
    # the bar sits there. A row whose sentence states a duration wants this one;
    # without the duration it wants the other.
    "seconds_after_resource_full": (0.0, 60.0, "a number of seconds"),

    # "When your class resource hits zero, gain 20%-40% increased damage for 5
    # seconds" is `seconds_after_resource_empty` with 5. Issue #1815.
    #
    # THE POOL REACHING ZERO, NOT A MAXIMUM OF ZERO. A maximum of zero is a
    # character with no class resource at all, which is a different thing and is
    # why the full condition refuses it.
    "seconds_after_resource_empty": (0.0, 60.0, "a number of seconds"),

    # "Your cooldowns reset 50%-100% faster when fighting Boss enemies, for 4
    # seconds after you strike one" is `seconds_after_striking_a_boss` with 4.
    #
    # THE ONLY CLOCK HERE THAT TURNS ON WHAT WAS STRUCK rather than on what the
    # character did. `target_is_boss` cannot serve that sentence: it is answered
    # from the character being struck, and a cooldown counts down with nobody
    # being struck, so nothing fills a target for it to read.
    #
    # THE WINDOW IS THE OWNER'S, THE FOUR SECONDS ARE A JUDGEMENT. The project
    # owner chose a window over a proximity check on 2026-09-18, with the
    # proximity shape Path of Exile ships put to them and declined. Four is what
    # Path of Exile 1 and 2 both define "Recently" as, and it sits inside the 2
    # to 5 seconds this game's own rows already use.
    "seconds_after_striking_a_boss": (0.0, 60.0, "a number of seconds"),
    # Using your support ability grants you 10%-20% increased movement speed for 3 seconds: `seconds_after_support_skill`.
    # Issue #1815. The character used the skill in its Support slot.
    "seconds_after_support_skill": (0.0, 60.0, "a number of seconds"),
    # After you use a movement ability you lose 50% movespeed for 3 seconds: `seconds_after_movement_skill`.
    # Issue #1815. The character used the skill in its Movement slot.
    "seconds_after_movement_skill": (0.0, 60.0, "a number of seconds"),
    # Casting a spell grants 5%-10% increased movement speed for 3 seconds: `seconds_after_spell`.
    # Issue #1815. The character used a skill carrying `Type.Spell`.
    "seconds_after_spell": (0.0, 60.0, "a number of seconds"),
    # After being hit by a melee attack you gain 10%-20% increased movement speed for 2 seconds: `seconds_after_melee_hit_taken`.
    # Issue #1815. The character was hit by a melee attack.
    "seconds_after_melee_hit_taken": (0.0, 60.0, "a number of seconds"),
    # Applying a CC effect grants 10%-20% increased movement speed for 3 seconds: `seconds_after_crowd_control`.
    # Issue #1815. The character stunned, knocked down or displaced a character.
    "seconds_after_crowd_control": (0.0, 60.0, "a number of seconds"),

    # "a skill whose health cost is above 10% of your maximum health" is
    # `skill_health_cost_above` with 10. Issue #983.
    #
    # THE ONLY ONE OF THESE THAT ASKS ABOUT THE SKILL RATHER THAN THE CHARACTER,
    # so one character using two skills an instant apart answers it differently
    # for each. Nothing the other three can express covers that.
    #
    # STRICTLY ABOVE, NOT AT OR BELOW, unlike `health_at_or_below`, and the
    # difference is reachable rather than pedantic: the Deeper Cuts node at its
    # full ten points adds exactly 10% of maximum health to every skill, so a
    # character with that node and no skill of its own cost sits precisely on
    # this threshold and correctly gets nothing.
    "skill_health_cost_above": (0.0, 100.0, "a percentage of maximum health"),

    # "While you are Bleeding" is `while_bleeding`, and it takes no value.
    # Issue #962. Thirst for Pain is the node.
    #
    # THE FIRST CONDITION THAT ASKS WHAT THE CHARACTER IS CARRYING rather than
    # where one of its own numbers stands, and the first that compares nothing.
    # `UCataclysmDebuffs::IsBleeding` is what answers it.
    #
    # ONE NAME PER KIND OF EFFECT, rather than one name and a column saying
    # which. The same argument the three stack scales below make: a further
    # column here is a row struct change, so a build has to happen before the
    # DataTable asset can be regenerated. This one could not use a column
    # anyway, because the value column holds a number.
    "while_bleeding": None,

    # "While your Fervour is at maximum" is `class_resource_at_maximum`, and it
    # takes no value either. Issue #1026. Communion of Pain is the node.
    #
    # NOT A THRESHOLD WITH THE VALUE SET TO A HUNDRED. A threshold has to be
    # either points or a percentage of the maximum, and the two disagree: the
    # Ritualist's `class_resource` is 150 where every other class's is 100. "At
    # maximum" is the top of whatever bar the class has and is neither.
    #
    # A FUTURE "while above 75 Fervour" WANTS ITS OWN NAME, for the same reason.
    # That one is a points threshold and this one is not, so reusing this with a
    # value would give a Ritualist the wrong answer.
    "class_resource_at_maximum": None,

    # "You take 20%-40% less damage from spells" is `hit_is_spell`, and it takes
    # no value. Issue #666. The other two name the rows' other sources.
    #
    # THE FIRST CONDITIONS THAT ASK ABOUT THE BLOW BEING TAKEN rather than the
    # character taking it. A hit records three separate facts -- melee, ranged
    # and spell -- and `UCataclysmDamageCalculation::Resolve` hands them to the
    # damage taken lookup, so these hold only for a hit from that source. The
    # character sheet has no hit in hand and shows none of them.
    #
    # AN ATTACK IS A HIT THAT IS NOT A SPELL. The rows say "melee attacks",
    # "ranged attacks" and "spells", and Path of Exile draws the line the same
    # way: a skill is an attack or a spell, and projectile is a separate tag
    # either kind can carry. So a spell that fires a projectile meets
    # `hit_is_spell` and not `hit_is_ranged_attack`. `docs/DECISIONS.md` has the
    # sources.
    "hit_is_melee_attack": None,
    "hit_is_ranged_attack": None,
    "hit_is_spell": None,

    # "You take 20%-40% less damage from Boss enemies" is `opponent_is_boss`,
    # and it takes no value. Issue #666. A boss is what
    # `ACataclysmEnemyCharacter::IsBoss` says: the Boss and Cataclysm Boss
    # rarities, and not a Herald, which is the line the stun rule already uses.
    "opponent_is_boss": None,

    # "Staggered enemies deal 15%-30% increased damage to you" is
    # `opponent_is_staggered`, and it takes no value. Issue #45. Staggered is the
    # `State.Staggered` tag a landed knockback, pull or knockdown leaves for a
    # second, and `UCataclysmSkillEffects::IsStaggered` is what answers.
    #
    # NOT LIMITED TO ENEMY CREATURES, unlike `opponent_is_boss` above. A boss is
    # a creature rarity; the Staggered state lands on anything a displacement
    # moves, the player included.
    "opponent_is_staggered": None,

    # "You take 15%-25% more damage from enemies that are currently CC'd" is
    # `opponent_is_crowd_controlled`, and it takes no value. Issue #1686.
    #
    # CROWD CONTROL IS THE SIX EFFECTS OF DESIGN SECTION VI, ruled under the
    # owner's delegation on 2026-09-23: stun, knockdown, slow, displacement,
    # pin and madness. `UCataclysmSkillEffects::IsCrowdControlled` answers.
    # Read off the attacker as an actor, beside `opponent_is_staggered`.
    "opponent_is_crowd_controlled": None,

    # "You take 15%-25% more damage from melee attacks while moving" is
    # `melee_hit_while_moving`, and it takes no value. Issue #1686, ruled on
    # #1697: ONE NAME FOR THE PAIR, as `target_carries_cripple_and_weaken` is,
    # because a row carries one condition and two rows would add.
    "melee_hit_while_moving": None,

    # "Staggered enemies take 20%-35% increased damage from all sources" is
    # `target_is_staggered`, and it takes no value. Issue #45. It is the mirror
    # of `opponent_is_staggered` above: the same state, asked from the other end
    # of the blow.
    #
    # TWO NAMES AND NOT ONE, for the reason `target_within_metres` and
    # `opponent_beyond_metres` are two. The engine fills the blow record only on
    # the defender's damage taken lookup and the target reading only on the
    # attacker's own lookups, so a row carrying the wrong one of this pair reads
    # a field nothing filled and grants nothing, rather than quietly answering
    # with the staggered state of the character at the other end.
    #
    # "FROM ALL SOURCES" IS THE WEARER'S OWN DAMAGE ACROSS ITS TYPES, which is
    # why the row is written as an `attack_damage` row and a `spell_damage` row
    # rather than as anything applied to the enemy. The row carries
    # `Stat.Offense.Global`, as do both other rows whose words use that phrase
    # about enemies.
    "target_is_staggered": None,
    # "You deal 15%-30% increased damage to enemies standing in your persistent
    # AOE zones" is `target_in_your_zone`: the character struck stands in a
    # ground zone the wearer's own skill left. Ruled 2026-10-06. IT TAKES NO
    # VALUE. It does not reach the zone's own damage, which is priced with no
    # target.
    "target_in_your_zone": None,
    # "Enemies in your Siphon take 1% increased damage from you per point for
    # every 50 Fervour you hold" is `target_in_your_siphon`: the character struck
    # is in the asker's Siphon (`FCataclysmSiphon` keeps who is). The Death
    # trees' slice C2, 2026-11-25. IT TAKES NO VALUE.
    "target_in_your_siphon": None,

    # "Deal 30%-50% more damage to Boss enemies" is `target_is_boss`, and it
    # takes no value. Issue #1815. It is the mirror of `opponent_is_boss` above:
    # the same fact, asked from the other end of the blow.
    #
    # THE PAIR EXISTED WITH ONLY ONE HALF UNTIL NOW, which is what made this
    # worth building. The engine filled `opponent_is_boss` from the incoming
    # hit, so it answers "a boss hit me"; nothing answered "I am hitting a
    # boss", and three enchantment sentences about damage to bosses had nowhere
    # to go. A row carrying the wrong half of the pair reads a field nothing
    # filled and grants nothing, exactly as the staggered pair above warns.
    "target_is_boss": None,

    # "You deal 20%-35% less damage to non-Boss enemies" is `target_is_not_boss`,
    # and it takes no value. Issue #1815, the project owner's decision of
    # 2026-09-18 to add a second condition rather than a negation flag on the
    # column.
    #
    # NOT THE PLAIN OPPOSITE OF `target_is_boss`. A modifier carries exactly one
    # condition, so the penalty cannot be written as "not target_is_boss"
    # anywhere; and in the engine both halves refuse when nothing read the
    # target, so neither fires on a character sheet with no target in hand.
    "target_is_not_boss": None,

    # "Your first hit against each enemy ..." is `target_not_yet_struck_by_you`,
    # and "Your first critical strike against each enemy ..." is
    # `target_not_yet_crit_by_you`. Issue #1815. Neither takes a value: "first"
    # names a state of the target's record, not a number. A hit is a blow that
    # got through, ruled under the owner's delegation on 2026-09-23.
    "target_not_yet_struck_by_you": None,
    "target_not_yet_crit_by_you": None,

    # "HP regeneration is disabled during combat" is `in_combat`, and "HP
    # regeneration is doubled while out of combat" is `out_of_combat`. Issue
    # #1815. Neither takes a value: one meaning of "in combat" for the whole
    # game, a hit dealt or taken within the engine's
    # `CombatLapseSeconds` (3), ruled under the owner's delegation on 2026-09-23.
    "in_combat": None,
    "out_of_combat": None,

    # "After 10 seconds in combat you begin losing 2%-4% of your maximum HP per
    # second" is `in_combat_for_seconds` with 10. Ruled 2026-10-07. The reading
    # `in_combat` tests, compared AT LEAST, as `stationary_for_seconds` compares
    # the reading `while_stationary` tests. Out of combat it refuses whatever
    # the value, and a new combat starts the count from nought.
    #
    # THE SAME 0 TO 60 SECOND BOUND the seconds conditions use.
    "in_combat_for_seconds": (0.0, 60.0, "a number of seconds"),

    # "Gain 50%-100% increased damage while under the effect of a DoT" is
    # `while_under_damage_over_time`, and it takes no value. Ruled 2026-10-07.
    # The parent of the tag `while_bleeding` asks for: any effect that
    # `UCataclysmSkillEffects::ApplyDamageOverTime` laid on the character,
    # whoever laid it, the character itself included. It asks only the
    # wearer's own state, so every asker can judge it.
    "while_under_damage_over_time": None,

    # "Channel skills drain 8%-15% of your maximum HP per second while active"
    # and "You cannot move while channeling any skill" are `while_channelling`,
    # and it takes no value. Ruled 2026-10-08. A skill of the wearer's own
    # tagged `Type.Channel` is running, from the moment it is paid for until it
    # ends, whatever ended it. It asks only the wearer's own state, so every
    # asker can judge it.
    "while_channelling": None,

    # "Channel skills deal 30%-50% less damage during the first 2 seconds of
    # channeling" is `channelling_for_under_seconds` with 2. Ruled 2026-10-08.
    # STRICTLY LESS THAN, the one seconds condition that is: a blow landing
    # exactly 2 seconds in is whole. Not channelling refuses whatever the
    # value. The wearer's own state again.
    #
    # THE SAME 0 TO 60 SECOND BOUND the seconds conditions use.
    "channelling_for_under_seconds": (0.0, 60.0, "a number of seconds"),

    # "Debuffed enemies take 10%-20% increased damage from all sources" is
    # `target_carries_any_debuff`, and "Strike skills deal 20%-40% increased
    # damage against enemies affected by a DoT" is `target_carries_a_dot`. Issue
    # #1815. Neither takes a value. A debuff is what `UCataclysmDebuffs` counts.
    "target_carries_any_debuff": None,
    "target_carries_a_dot": None,

    # "Enemies you have damaged in the last 2 seconds" is
    # `target_damaged_by_you_within_seconds` with 2, for Set Upon and Set the
    # Pack On. Issue #1515. It reads the record `target_not_yet_struck_by_you`
    # reads, with the time kept, so "damaged" and "a hit" mean one thing.
    "target_damaged_by_you_within_seconds": (0.0, 60.0, "a number of seconds"),

    # "While moving" is `while_moving` and "while stationary" is
    # `while_stationary`, and neither takes a value. Issue #41, slice 2.
    #
    # TWO NAMES RATHER THAN ONE WITH A FLAG, because a row reads as its own
    # sentence: an enchantment says "while moving you deal increased damage" and
    # another says "while stationary you take less damage", and neither should
    # have to be written as the negation of the other.
    #
    # NEITHER IS THE OTHER'S NEGATION IN THE ENGINE EITHER. A caller with no
    # character in hand, such as the character sheet, is refused by both.
    "while_moving": None,
    "while_stationary": None,

    # "After remaining stationary for 3 seconds" is `stationary_for_seconds`
    # with 3, and the comparison is AT LEAST: three seconds of standing still
    # meets a threshold of three. Issue #41, slice 2.
    #
    # THE SAME BOUND AS THE TWO WINDOWS ABOVE, and for the same reason: the
    # design's longest wait is a few seconds, and anything past a minute is
    # likelier to be a number in the wrong column than a deliberate wait.
    "stationary_for_seconds": (0.0, 60.0, "a number of seconds"),

    # "While you have not attacked in the last 3 seconds" is
    # `not_attacked_for_seconds` with 3, also AT LEAST. Issue #41, slice 2.
    #
    # THE CHARACTER'S OWN ATTACK, not a blow it took, which
    # `seconds_after_foreign_damage` above covers. Every attack a character
    # makes resets it, creatures and minions included, so the reading means
    # the same thing on both sides of a fight.
    "not_attacked_for_seconds": (0.0, 60.0, "a number of seconds"),

    # "Your first melee attack after moving 5 metres" is
    # `metres_moved_before_attack` with 5, AT LEAST again. Issue #41, slice 2.
    #
    # "FIRST" IS THE RESET RATHER THAN A FLAG. The distance is counted since the
    # character's own last attack, so the first attack after moving five
    # metres reads five or more and the next reads about nothing. That is what
    # makes this a condition rather than an event.
    #
    # METRES, WHICH IS WHAT THE ROWS SAY, while the engine measures in
    # centimetres. `UCataclysmMovement` divides once, where the sample is taken.
    #
    # A HUNDRED METRES IS THE SANITY LIMIT, not a design rule: the longest
    # distance any row states is five, and a floor is not a hundred metres of
    # walking between two attacks.
    "metres_moved_before_attack": (0.0, 100.0, "a number of metres"),
    # "You take 25% less damage from enemies more than 6 metres away from you"
    # is `attacker_beyond_metres` with 6. Standing Apart, the Ritualist's
    # 100-point capstone third option, is the node.
    #
    # NAMED FOR THE ATTACKER RATHER THAN THE OPPONENT, unlike the row above,
    # and the difference is worth the inconsistency. `opponent_is_boss` reads
    # the same from either side of a blow; a distance does not. This one is the
    # DEFENDER reading how far away the character hitting it stood, and a row
    # wanting the reverse -- the attacker reading how far away its target is,
    # which Brute's Heart and Demon King's Regalia both need -- is a different
    # mechanism entirely, because the blow context reaches only the damage
    # taken lookup. Naming this one "opponent" would invite a designer to write
    # it on an attacker's row and get nothing. THAT REVERSE READING NOW EXISTS
    # as `target_within_metres` below; this comment said it did not.
    #
    # STRICTLY MORE THAN, BECAUSE THE NODE WRITES "more than". A character at
    # exactly 6 metres takes full damage, the same boundary
    # `skill_health_cost_above` draws.
    #
    # THE UPPER BOUND IS 100 METRES, which is a judgement rather than a figure
    # read off anything. Nothing in the design states a distance beyond which a
    # character cannot be hit, and the largest distance any authored row names
    # is 10 metres. A hundred is well past anything a skill reaches, so a
    # threshold above it would be a row nothing could ever satisfy.
    "attacker_beyond_metres": (0.0, 100.0, "a distance in metres"),
    # "You gain 25% increased damage against enemies that are within 5 meters of
    # you" is `target_within_metres` with 5. Brute's Heart's 2-piece set bonus is
    # the row, and Demon King's Regalia's says the same with "more" instead of
    # "increased". Issue #1596.
    #
    # THE MIRROR OF THE ROW ABOVE, READ FROM THE OTHER END OF THE BLOW. That one
    # is the DEFENDER asking how far away the character hitting it stood; this is
    # the ATTACKER asking how far away its target is. They are separate names
    # because they are separate readings carried by separate routes, and a row
    # using the wrong one of the two gets nothing rather than a number from the
    # wrong end.
    #
    # AT OR WITHIN, BECAUSE BOTH ROWS WRITE "within 5 meters". A target standing
    # at exactly 5 metres earns the bonus. That is the opposite boundary from
    # `attacker_beyond_metres`, whose node writes "more than", and the pair is
    # deliberate: `health_at_or_below` and `health_below` are kept apart for the
    # same reason.
    #
    # THE MELEE SCOPE TAG ON BOTH ROWS IS NOT HONOURED, RULED BY THE PROJECT
    # OWNER ON 2026-09-12, so `RequiredTags` stays empty and any attack type
    # earns the bonus within the distance. Honouring it would have made the
    # condition nearly always true: the longest melee weapon shape reaches 3.3
    # metres and enemies default to 2. Issue #1620 carries the scope tags in
    # general, which nothing enforces.
    #
    # THE SAME 0 TO 100 METRE BOUND AND THE SAME JUDGEMENT BEHIND IT as the row
    # above. A threshold past 100 would be a row nothing could satisfy.
    "target_within_metres": (0.0, 100.0, "a distance in metres"),

    # "while an enemy is within 4 metres" is
    # `enemies_in_reach_at_least` with 1, and "while three or more enemies
    # are within 4 metres" is the same name with 3. Issue #1597.
    #
    # ONE NAME FOR BOTH SENTENCES, because the first is the special case of
    # the second where the count is one. Two names would be two spellings of
    # one reading.
    #
    # THE VALUE IS THE COUNT AND NOT THE RADIUS. The radius is a column of
    # its own, because a condition carries one number and this sentence needs
    # two. `Reach Metres` is that column and it is not built yet; the refusal
    # in `passive_effects` below is what stops a row being authored before it
    # exists.
    #
    # AT LEAST ONE, BECAUSE AT LEAST NOUGHT IS TRUE OF AN EMPTY ROOM. A row
    # whose count was left blank would otherwise grant its bonus everywhere,
    # silently and in the player's favour. The engine refuses it a second
    # time for the same reason.
    #
    # THE UPPER BOUND IS A JUDGEMENT, in the shape the windows above use. The
    # highest count any authored node states is three. A threshold past
    # twenty is far likelier to be a distance written in the count column
    # than a node that means it.
    "enemies_in_reach_at_least": (1.0, 20.0, "a number of enemies"),

    # "When they hit three or more enemies at once" is `enemies_hit_at_least`
    # with 3. Issue #1515, for `Ravager_keystone_b_kA` Sundering.
    #
    # THE ENEMIES ONE ATTACK STRUCK TOGETHER, ruled on 2026-09-16: one group of
    # blows dealt by one use, counted after the skill's target cap and before
    # any blow resolves, so an enemy that evades still counts. The engine's
    # `FCataclysmStatConditions::EnemiesStruckTogether` carries the reason.
    #
    # BOUNDED AS THE REACH COUNT ABOVE IS, for the same two reasons: at least
    # nought is true of every blow, and a threshold past twenty is far likelier
    # to be a mistake than a node that means it.
    "enemies_hit_at_least": (1.0, 20.0, "a number of enemies"),

    # "Point blank AOE skills deal 15%-25% less damage to a single target" is
    # `enemies_hit_at_most` with 1. Issue #1686. The same count as the name
    # above, compared the other way, because the pipeline has no negation.
    # "A single target" is exactly one enemy struck, ruled under the owner's
    # delegation on 2026-09-23. The same bounds, for the same reasons.
    "enemies_hit_at_most": (1.0, 20.0, "a number of enemies"),

    # THE FOUR PREDICATES THAT READ AN AILMENT ON THE OTHER CHARACTER, and the
    # first that read one at all. Issues #1515 and #1642.
    #
    #   "against Crippled enemies"                     target_carries_cripple
    #   "against enemies that are both Crippled
    #    and Weakened"                    target_carries_cripple_and_weaken
    #   "against enemies you have Weakened"          opponent_carries_weaken
    #   "enemies carrying a void splinter take
    #    increased damage from you"        target_carries_void_splinter
    #
    # NONE TAKES A VALUE, because each names its ailment. A predicate asking
    # "carrying at least N debuffs" would compare a number and is a different
    # question; nothing has asked for it.
    #
    # THE THIRD IS THE MIRROR OF THE FIRST TWO AND READS THE OTHER END OF THE
    # BLOW. `target_*` is filled only on the attacker's own lookups and
    # `opponent_*` only on the defender's damage taken lookup, exactly as
    # `target_is_staggered` and `opponent_is_staggered` are, so a row carrying
    # the wrong one of the pair grants nothing rather than answering about the
    # character at the other end.
    #
    # THE CONJUNCTION IS ONE NAME AND NOT TWO ROWS, and that is arithmetic.
    # `UCataclysmStatPipeline::Accumulate` sums increases, so a row for each
    # ailment would pay when EITHER is present and pay TWICE when both are.
    #
    # THIS SET DOES NOT SCALE AND IS NOT MEANT TO. Twenty-eight debuffs, two
    # ends of the blow and every combination is not a vocabulary. Add a name
    # when a node's own sentence names the ailment, never speculatively; at a
    # fourth and a fifth the right answer is a column naming the ailment on the
    # row instead, and `docs/DECISIONS.md` records that so whoever adds the
    # sixth reads it first.
    #
    # THE FOURTH WAS REACHED ON 2026-09-14 AND THE REVISIT HAPPENED, which is
    # what that rule asks for rather than a silent extension.
    # `docs/DECISIONS.md` carries it: one row asks, the parameterised column's
    # cost now falls on `FCataclysmEnchantmentEffectRow` rather than the passive
    # row struct the original analysis priced, and `ConditionValue` is a float
    # that cannot carry an ailment's identity. THREE NAMES WERE ASKED FOR AND
    # ONE WAS ADDED: the rows that looked like they needed a bleeding and a
    # poisoned target move a number on the ENEMY rather than on the attacker,
    # so they need an enemy-side modifier and not a predicate here.
    "target_carries_cripple": None,
    "target_carries_cripple_and_weaken": None,
    "opponent_carries_weaken": None,
    "target_carries_void_splinter": None,

    # AND ONE THAT NAMES THE SAME TWO AILMENTS FROM THE OTHER END. Issue #1718.
    # `Ravager_basic_c_c0` Spreading Hurt: "+4% increased Area of Effect per
    # point for attacks that Cripple or Weaken."
    #
    # NOT A TARGET-STATE PREDICATE, THOUGH IT SITS BESIDE THEM. An area of
    # effect shapes an attack BEFORE it lands, so whether a blow applied a
    # Cripple is not knowable when the bonus is worked out. What is knowable is
    # whether this character's attacks are ones that cripple or weaken, which is
    # whether either chance is above zero.
    #
    # IT TAKES NO VALUE for the same reason the four above do: it names its
    # ailments rather than comparing a number.
    "can_cripple_or_weaken": None,

    # "against enemies below 35% health" is `target_health_below` with 35, and
    # "against enemies below half health" is the same name with 50. Issue #1515.
    #
    # THE FIRST PREDICATE THAT READS SOMEBODY ELSE'S HEALTH. The four above it
    # -- `health_at_or_below`, `health_below`, `health_above`,
    # `health_at_or_above` -- are all the character's own, and
    # `skill_health_cost_above` is a cost. None could express a bonus against a
    # wounded enemy.
    #
    # STRICTLY BELOW, AND THERE IS NO INCLUSIVE TWIN. Both node sentences say
    # "below" and neither says "at or below". The character's own pair exists
    # because real nodes differ; the second name here waits for a node that
    # needs it.
    #
    # THE SAME 0 TO 100 BOUND AS THE CHARACTER'S OWN, because a share of maximum
    # health is between 0 and 100 whoever it belongs to. An unbounded threshold
    # is a row that never applies or always does, which is the silent failure
    # every entry here is bounded against.
    "target_health_below": (0.0, 100.0, "a percentage of maximum health"),

    # "while your Energy Shield is full" is `energy_shield_at_maximum`, and it
    # takes no value. Issue #1515. Cold Reading is the node.
    #
    # THE SECOND POOL TO ASK WHETHER IT IS FULL, after
    # `class_resource_at_maximum` above. Same answer, and NOT the same argument.
    # That one reasons from classes disagreeing with each other -- the
    # Ritualist's `class_resource` is 150 where every other class's is 100 --
    # and the Ritualist is the ONLY class with an energy shield, so there is no
    # disagreement between classes to point at here.
    #
    # THE REASON THAT DOES APPLY IS THE LEVEL CURVE. `max_energy_shield` is 40
    # with 8 added per level, so the top of the bar moves as a character grows:
    # a points threshold of 40 is exactly full at level one and about 36% of
    # the bar at level ten. Points and percentage still disagree, over one
    # character's lifetime rather than between two classes.
    #
    # THAT SAID 48 AND 40% AND WAS WRONG: IT USED THE MINION LEVEL FORMULA.
    # `UCataclysmClassStats::BaseFor` computes `Base + PerLevel * (Level - 1)`,
    # so a level 1 character has exactly the base; the minion helper computes
    # `Base + PerLevel * Level`. The bar is 40 at level one and 112 at level
    # ten. 48 is the level TWO maximum, which is why the wrong figure read as
    # plausible.
    #
    # A FUTURE "while your Energy Shield is above 75%" WANTS ITS OWN NAME, for
    # the same reason a future "while above 75 Fervour" does. That one is a
    # threshold and this one is not.
    "energy_shield_at_maximum": None,

    # "Nearby enemies deal 10%-30% less damage to you" is
    # `opponent_within_metres` with 5. Issue #1981.
    #
    # FIVE IS MEASURED AND NOT CHOSEN. Every row in the game that asks whether
    # another character is close uses 5.0 -- the six on `target_within_metres`,
    # and no other value appears in any table carrying a Condition column,
    # measured 2026-09-18. The sentence says only "nearby", so the figure had to
    # come from somewhere, and matching what is already shipped beats inventing.
    #
    # THE NEAR CASE OF THE BLOW'S OWN DISTANCE, where `opponent_beyond_metres` is
    # the far one. Both read the blow the DEFENDER took, which is the half
    # `target_within_metres` cannot answer: that one is the attacker's own
    # lookup. A row about enemies hitting you needs this one.
    #
    # THE SAME 0 TO 100 METRE BOUND AS ITS TWIN, and for that judgement's reason:
    # a threshold past 100 would be a row nothing could satisfy.
    "opponent_within_metres": (0.0, 100.0, "a distance in metres"),

    # "Strike skills deal 25%-40% less damage if you have moved in the last 2
    # seconds" is `moved_within_seconds` with 2. Issue #1981.
    #
    # THE OTHER SIDE OF `stationary_for_seconds`, WHICH IS NOT ITS NEGATION. That
    # one holds at AT LEAST the threshold and this at AT MOST it, so at exactly
    # the threshold both hold. One instant wide, and said rather than hidden.
    "moved_within_seconds": (0.0, 60.0, "a number of seconds"),

    # "When your class resource is above 75%, all skills cost 20%-40% less mana"
    # is `class_resource_above` with 75. Issue #1981.
    #
    # A SHARE AND NOT A COUNT OF POINTS. Classes do not share a maximum, so a row
    # written in points would mean a different fraction of the bar for each of
    # them. `class_resource_held` is the scale that reads the points themselves.
    #
    # STRICTLY ABOVE, the boundary `health_above` draws for the same word.
    "class_resource_above": (0.0, 100.0, "a percentage of the maximum class resource"),
    # ITS COMPLEMENT, "at or below", the Saboteur's rows of 2026-11-25: Durable
    # Modifications' 3% a point at or below 50%, beside its 6% above.
    "class_resource_at_or_below": (0.0, 100.0, "a percentage of the maximum class resource"),

    # "While you have 50 or more Fervour, you gain +2% increased Damage Reduction
    # and +3% increased Attack Damage per point" is the Ravager's Set Against It.
    # Issue #1515.
    #
    # A COUNT OF POINTS, WHICH IS THE OPPOSITE OF THE ENTRY ABOVE, and the reason
    # is measured rather than argued. That one says classes do not share a
    # maximum; true, and not the reason here, because this node is in the Ravager
    # tree and only a Ravager takes it. The reason is that ONE character's own
    # bar moves: `class_resource` gains nothing per level, and is raised by an
    # affix, by enchantments and by fourteen passive rows, so a threshold written
    # as a percentage would demand more Fervour the more of it a player could
    # hold. The sentence says fifty Fervour.
    #
    # THE BOUND IS WIDE ON PURPOSE. The largest bar in the data today is 150, and
    # a bound there would refuse a row written for a bar that investment has
    # already made bigger. A thousand refuses nothing worth refusing and still
    # catches a percentage typed into a points column by mistake only if it is
    # absurd -- which is the honest limit of what a bound can do here.
    "class_resource_points_at_least": (0.0, 1000.0,
                                       "a count of class resource points"),

    # "You take 10%-20% increased damage from all sources while your shield is
    # active" is `energy_shield_above_zero`, and it takes NO VALUE. Issue #1981.
    #
    # "ACTIVE" IS HELD ABOVE ZERO, a judgement under the project owner's
    # delegation of 2026-09-14, recorded in docs/DECISIONS.md. A shield at
    # nothing absorbs nothing.
    #
    # NOT THE SAME QUESTION AS `energy_shield_at_maximum` ABOVE, which asks
    # whether the bar is full. This asks whether there is any bar left at all.
    "energy_shield_above_zero": None,

    # "When your Mana falls below 10%" is `mana_below` with 10. Issues #1820 and
    # #41. STRICTLY BELOW, the reading `health_below` takes of the same word, and
    # a character with no maximum mana is never below anything.
    #
    # A GENERAL THRESHOLD ON PURPOSE. The dungeon floor rule
    # `Famine_Desperate_Measures` asks it from C++ at 10, and the enchantment
    # "Take 10%-40% more damage when on low mana" is to take a row on it.
    "mana_below": (0.0, 100.0, "a percentage of maximum mana"),

    # "Two-handed weapons only" is `wielding_two_handed_weapon`. Issue #1515,
    # the Ravager node Two Hands. TWO-HANDED IS THE ITEM BASES TABLE'S OWN
    # `Hands` COLUMN, read off the equipped weapon's row; no weapon, or a weapon
    # type with no row, refuses rather than being guessed.
    #
    # IT TAKES NO VALUE: it names a state rather than comparing a number.
    "wielding_two_handed_weapon": None,

    # "Dual wield only" is `dual_wielding`: two items in the weapon slots, a
    # weapon and a Shield among them (the design document's Dual Wielding: "A
    # character holding a weapon and a Shield is dual wielding"). The
    # Berserker's states, 2026-11-25, for Frenzy and the fifteen other Berserker
    # entries that say it. No equipment, which is every enemy, refuses.
    #
    # "While in Wrath" and "While in Frenzy" are `in_wrath` and `in_frenzy`: the
    # state `FCataclysmBerserkerStates` begins at a full pool and ends at an
    # empty one. Each takes no value: it names a state.
    "dual_wielding": None,
    "in_wrath": None,
    "in_frenzy": None,

    # "Enemies that survive more than 5 seconds after first being hit gain
    # 20%-40% increased damage" is `opponent_first_struck_by_you_seconds_ago`
    # with 5, on a `damage_taken` row. Ruled 2026-10-12 under the owner's
    # delegation. It asks about the character on the other side of a blow
    # being TAKEN: whether the wearer's first blow got through to it at least
    # that many seconds ago. An opponent the wearer has never struck refuses
    # whatever the value, and so does a damage over time tick, which carries
    # no blow record.
    #
    # AT LEAST, as `in_combat_for_seconds` and `stationary_for_seconds`
    # compare. The sentence writes "more than"; the strict comparison is the
    # option not taken, and `docs/DECISIONS.md` of that day says why.
    #
    # THE SAME 0 TO 60 SECOND BOUND the seconds conditions use.
    "opponent_first_struck_by_you_seconds_ago": (0.0, 60.0, "a number of seconds"),

    # "Your attacks have a 25% chance to silence an enemy for 1 second" is
    # `hit_dealt_is_not_spell` on an `apply_status` row. Ruled 2026-10-17 (O6):
    # "your attacks" is every hit that is not a spell, the line between an
    # attack and a spell the three `hit_is_` conditions draw. IT TAKES NO VALUE.
    #
    # AN ACTION ROW'S CONDITION ONLY, on an event carrying the tags of the hit
    # dealt (`EVENTS_CARRYING_A_HIT_DEALTS_TAGS`). The engine judges it from the
    # event's own tags in `UCataclysmAbilitySystemComponent::PoolActionAllowed`;
    # no stat lookup is handed those, so it is refused on every stat row
    # (`CONDITIONS_OF_AN_ACTIONS_EVENT`). A hit no skill dealt carries no tags,
    # and is not a spell.
    "hit_dealt_is_not_spell": None,

    # "Every time you hit a silenced enemy, you gain a stack of 'Void.'" is
    # `target_is_silenced` on a `kept_stacks_free_next_ability` row on
    # `hit_dealt`. Ruled 2026-10-17 (O7). The character struck is silenced
    # (`UCataclysmSilence::IsSilenced`). IT TAKES NO VALUE. A fact of the target
    # in hand, read by `WithTargetState` as `target_is_boss` is, so it is one of
    # `CONDITIONS_OF_A_HIT_DEALT`: an action row judges it against the enemy its
    # event names, and a stat row on a stat asked while a hit is priced.
    "target_is_silenced": None,

    # "While the enemy is moving, your DoTs on them deal 20%-40% more damage" is
    # `dot_damage` more under `target_is_moving`. Ruled 2026-10-23 (#2235, Q1):
    # the character struck is moving, its own ability system's movement sample.
    # IT TAKES NO VALUE. A fact of the target in hand, read by `WithTargetState`
    # as `target_is_silenced` is, so it is one of `CONDITIONS_OF_A_HIT_DEALT`.
    # NOT `while_moving`, which reads the wearer's own movement.
    "target_is_moving": None,

    # "+20% increased Spell Damage against enemies carrying your curse" and
    # "your minions deal 20% increased damage to enemies carrying your curse",
    # Subjugator and Long Servitude in the Ritualist tree since the demonic tree
    # rework of 2026-11-24. The character struck carries a `Status.Debuff.*` the
    # asker laid (`UCataclysmRisenImps::CarriesCurseLaidBy`); for a minion's blow
    # the asker is its summoner. IT TAKES NO VALUE. A fact of the target in hand,
    # read by `WithTargetState` as `target_is_moving` is, so it is one of
    # `CONDITIONS_OF_A_HIT_DEALT`. NOT `target_carries_any_debuff`, which a burn
    # satisfies.
    "target_carries_your_curse": None,
    # "Killing an enemy that carries your Poison or Bleed" is
    # `target_carries_your_poison_or_bleed`: the Shadow's Death's Patience, the
    # Death trees' slice C4a (2026-11-25). A Poison or a Bleed whose effect names
    # the asker as its instigator (`UCataclysmDebuffs::CarriesPoisonOrBleedLaidBy`),
    # as the curse above. IT TAKES NO VALUE, and it is one of
    # `CONDITIONS_OF_A_HIT_DEALT`, a fact of the target in hand.
    "target_carries_your_poison_or_bleed": None,
    # "Fervour gained from sources other than kills" is
    # `fervour_gain_not_from_a_kill`: the Soul Collector's Any Fuel, the Death
    # trees' slice C4b1 (2026-11-25), a row on `class_resource_generation`, the
    # rate every gain is scaled by. It holds unless a kill's Fervour is being paid
    # (`UCataclysmAbilitySystemComponent::IsPayingForAKill`). IT TAKES NO VALUE. A
    # fact of the gain in hand, read by `CurrentConditions`, so not one of
    # `CONDITIONS_OF_A_HIT_DEALT`.
    "fervour_gain_not_from_a_kill": None,
    # "the damage your hit deals" is `killing_blow_was_a_hit`: the Soul
    # Collector's Passed On, the Death trees' slice C4c1 (2026-11-25), as ruled.
    # True while the player's kill handler acts on an own kill made by a hit and
    # not by a tick (`FCataclysmDeathNotice::bByDamageOverTime`), and nowhere
    # else. IT TAKES NO VALUE. Read by `CurrentConditions` from that mark, so
    # not one of `CONDITIONS_OF_A_HIT_DEALT`.
    "killing_blow_was_a_hit": None,
    # "Enemies within 3 metres of an enemy you kill ... for 3 seconds" is
    # `target_near_your_kill_within_seconds`: the Soul Collector's Grim Wake,
    # the Death trees' slice C4c2 (2026-11-25), as ruled. A record kept on the
    # character marked, keyed by the killer, written at the kill for every enemy
    # within the row's `Reach Metres` of the body. The value is the seconds. A
    # fact of the target in hand, so one of `CONDITIONS_OF_A_HIT_DEALT`.
    "target_near_your_kill_within_seconds": (0.0, 60.0, "a number of seconds"),
    # "those kills give double Fervour" is `kill_was_an_execute`: the Soul
    # Collector's Reaper's Edge, the Death trees' slice C4d1 (2026-11-25), as
    # ruled. The kill handler marks a kill whose death notice says it was an
    # execute while it pays for it and acts on it. It names a state.
    "kill_was_an_execute": None,
    # "your next hit is a critical strike and deals +5% increased damage" is
    # `next_hit_is_critical`: the Shadow's Brim, the Death trees' slice C6a
    # (2026-11-25). The character holds a next-hit critical strike charge. It
    # names a state.
    "next_hit_is_critical": None,
    # THE FOUR OF THE DEATH TREES' SLICE C6b1 (2026-11-25), each a fact of the
    # target in hand, so each is one of `CONDITIONS_OF_A_HIT_DEALT`: From
    # Behind's "struck from behind"; Opening Strike's "at full health"; Venomed
    # Prey's "carrying your Poison", a Bleed alone not counting; and Hit and
    # Run's "more than 4 metres from you", strictly, the value the metres.
    "target_struck_from_behind": None,
    "target_at_full_health": None,
    "target_carries_your_poison": None,
    "target_beyond_metres": (0.0, 100.0, "a distance in metres"),
    # THE TWO OF THE DEATH TREES' SLICE C6b2 (2026-11-25), each a clock whose
    # value is the seconds. Arrival's "within 1 second after your movement
    # skill ends" counts from the END of a use that was paid for, where
    # `seconds_after_movement_skill` counts from the use; its event,
    # `movement_skill_ends`, is raised there. Glancing Cuts' "Enemies your
    # movement skill hits ... for 4 seconds" is a record kept on the character
    # struck, keyed by the striker, so a fact of the target in hand and one of
    # `CONDITIONS_OF_A_HIT_DEALT`.
    "seconds_after_movement_skill_ends": (0.0, 60.0, "a number of seconds"),
    "target_struck_by_your_movement_skill_within_seconds": (0.0, 60.0, "a number of seconds"),
    # THE DEATH TREES' SLICE C6d2 (2026-11-25), as ruled (P1): Bone Knit's
    # "while one of your minions has died in the last 3 seconds", a clock whose
    # value is the seconds. Its event, `minion_death`, is raised by
    # `UCataclysmAbilitySystemComponent::NoteMinionDied` for a minion, an imp or
    # a thrall killed, and not for a gadget or a trap.
    "seconds_after_minion_death": (0.0, 60.0, "a number of seconds"),
    # AND DEATH WARD'S "for 3 seconds after one of your minions dies within 6
    # metres of you", the same deaths with how far from the character each fell,
    # read within the row's `Reach Metres`. The value is the seconds.
    "minion_death_in_reach_within_seconds": (0.0, 60.0, "a number of seconds"),
    # AND AVENGING DEAD'S "When a hit takes more than 5% of your maximum health",
    # as ruled (Q2): the share of maximum health the hit being announced as
    # `hit_taken` took from health. AN EVENT CONDITION: it exists only while
    # `hit_taken` is raised, so on a row scaled by its own stacks the game judges
    # it at the grant and not at the stat (`IsJudgedAtItsEvent`), and it is
    # refused anywhere else (`EVENT_CONDITIONS` below). The value is a share in
    # per cent.
    "hit_took_at_least_percent_of_max_health": (0.0, 100.0, "a share of maximum health in per cent"),
    # AND MARSHAL'S "Your minions within 10 metres of you deal +2% increased
    # damage", the Death trees' slice C6d2 (2026-11-25): how far the minion whose
    # damage is asked stands from its summoner, which the summoner's lookup is
    # handed for a minion's or a thrall's blow and explosion. The value is metres.
    "minion_within_metres_of_you": (0.0, 100.0, "a number of metres"),
    # THE THREE OF THE DEATH TREES' SLICE C6b3 (2026-11-25). Read the Blow's
    # "attacks from Elite and stronger enemies" is `opponent_rarity_at_least`,
    # the attacker's rung on a blow taken, the value the rung. Vigil Over the
    # Strong's "While an Elite or stronger creature is within 15 metres" and
    # Reaper's Due's "while a Boss is within 15 metres" are
    # `enemy_of_rarity_in_reach`, the value the rung, the radius the row's Reach
    # Metres. Hunting Ground's "While no enemy is within 20 metres" is
    # `no_enemy_in_reach`, which names a state; its radius is the Reach Metres.
    "opponent_rarity_at_least": (0.0, 5.0, "a rarity rung, 0 Common to 5 Cataclysm Boss"),
    "enemy_of_rarity_in_reach": (0.0, 5.0, "a rarity rung, 0 Common to 5 Cataclysm Boss"),
    "no_enemy_in_reach": None,
    # "Kills within 2 seconds of each other ... The same applies to Soul Tide"
    # is `kill_chain_within_seconds`: the kill clock, its value lengthened by
    # Unbroken Chain's `kill_chain_seconds`. The Death trees' slice C6c
    # (2026-11-25), as ruled. NOT NAMED `seconds_after_...`, which would name an
    # action event the game does not raise. The value is the seconds.
    "kill_chain_within_seconds": (0.0, 60.0, "a number of seconds"),
    # THE TWO OF THE DEATH TREES' SLICE C6d1 (2026-11-25), as ruled. "While
    # three (or five) or more of your minions are within 6 metres of you" is
    # `minions_in_reach_at_least`, the value the count, the radius the row's
    # Reach Metres. "Enemies that have Necrosis" is `target_carries_necrosis`, a
    # fact of the target in hand, so one of `CONDITIONS_OF_A_HIT_DEALT`.
    "minions_in_reach_at_least": (1.0, 20.0, "a number of minions"),
    "target_carries_necrosis": None,
    # AND THE SHADOW'S DEATHMARK'S "It takes 30% more damage from you, and each
    # critical strike on it gives 5 Fervour": the character struck carries one
    # of the asker's own marks. The Death trees' slice C6e (2026-11-25), as ruled
    # (Q5). It names a state. A fact of the target in hand.
    "target_marked_by_you": None,
    # AND GUARD THE MASTER'S "deal +3% increased damage per point to it": the
    # character struck is the last that hit the asker, for a minion's blow its
    # summoner. The Death trees' slice C6e (2026-11-25). A fact of the target in
    # hand.
    "target_is_your_last_attacker": None,
    # "Killing an Elite or stronger creature gives you its modifiers for 20
    # seconds" is `wearing_enemy_modifier`: the Soul Collector's Wearer of the
    # Dead, the Death trees' slice C4d3 (2026-11-25), as ruled. The value is the
    # modifier's place in `WORN_ENEMY_MODIFIERS`, counted from 1, so the bounds
    # are that list's ends: the upper one is set from the list's length below
    # `WORN_ENEMY_MODIFIERS`, so adding to the list moves it.
    "wearing_enemy_modifier": (1.0, 1.0, "a place in WORN_ENEMY_MODIFIERS, counted from 1"),

    # THE DEATH TREES OF 2026-11-24. The rarity rung of the creature struck or
    # slain, 0 Common to 5 Cataclysm Boss (`game/Data/EnemyRarities.csv`):
    # "Elite or stronger" is `target_rarity_at_least` 1, and the Soul Collector's
    # "1 for each enemy you kill, 3 for an Elite, 5 for a Legendary, 8 for a
    # Herald" is one `target_rarity_is` row a rung, with "15 for a Boss" at least
    # 4. The rung the spawner set (`ACataclysmEnemyCharacter::RarityStep`), read
    # by `WithTargetState` as `target_is_boss` is, so both are
    # `CONDITIONS_OF_A_HIT_DEALT`. Anything that is not a creature has no rung and
    # refuses.
    "target_rarity_at_least": (0.0, 5.0, "a rarity rung, 0 Common to 5 Cataclysm Boss"),
    "target_rarity_is": (0.0, 5.0, "a rarity rung, 0 Common to 5 Cataclysm Boss"),

    # "For 4 seconds after you kill" is `seconds_after_kill` with 4: Between the
    # Dead, Killing Pace and Light Feet, the Death trees of 2026-11-24. Stamped
    # at a kill credited to the character (`UCataclysmFervour::NoteOwnKill`), the
    # stamp the Death classes' decay grace is measured from. The wearer's own
    # clock, so any asker judges it.
    "seconds_after_kill": (0.0, 60.0, "a number of seconds"),
    # "for 4 seconds after you spend Fervour" is `seconds_after_fervour_spent`:
    # Quick Kindling and Burnt Offering. The Death trees' slice C3a
    # (2026-11-25). Stamped by `UCataclysmAbilitySystemComponent::
    # NoteFervourSpent` where a skill pays its Fervour cost; it derives the event
    # `fervour_spent`, which carries the amount.
    "seconds_after_fervour_spent": (0.0, 60.0, "a number of seconds"),
    # "while you hold 20 or more Fervour" in a Soul Collector vow is
    # `fervour_threshold_reached` with 20: the Death trees' slice C3d
    # (2026-11-25). Its value is the FIGURE of one of the four Fervour thresholds,
    # and nothing else (`FERVOUR_THRESHOLDS`, refused otherwise below), because
    # Generous Count, Long Shadow and Steady Count reach those four and no other
    # "N or more Fervour". Worked out once a conditions fill by
    # `UCataclysmAbilitySystemComponent::FervourThresholdsReachedFor`.
    "fervour_threshold_reached": (20.0, 100.0, "the figure of a Fervour threshold"),
    # "while you hold fewer than 20 Fervour" is `class_resource_points_below`
    # with 20: the Soul Collector's Empty Hands, the Death trees' slice C3e
    # (2026-11-25). Strictly below, the opposite of
    # `class_resource_points_at_least`, and on the same Fervour counted as held
    # (Chained Souls' share included); the same wide bound, for its reason.
    "class_resource_points_below": (0.0, 1000.0, "a count of class resource points"),

    # THE SABOTEUR'S DEVICE CONDITIONS, 2026-11-25, on the coordinating
    # session's rulings of that day.
    #
    # "while within 15 meters of at least one active gadget" (Sentry Network) is
    # `own_gadget_within_metres` with 15: the nearest gadget the wearer commands,
    # measured from the wearer. The same 0 to 100 metre bound the other distance
    # conditions use.
    "own_gadget_within_metres": (0.0, 100.0, "a distance in metres"),
    # "against enemies that have not yet triggered one of your traps" (Ambush)
    # is `target_not_yet_triggered_your_trap`: the enemy struck has set off no
    # skill trap of the wearer's before the triggering whose blow it is taking
    # (a rearmed trap set off again by the same enemy is not its first).
    # IT TAKES NO VALUE. A fact of the target in hand, so it is one of
    # `CONDITIONS_OF_A_HIT_DEALT`.
    "target_not_yet_triggered_your_trap": None,
    # "while any of your gadgets are under attack" (Overcautious) is
    # `seconds_after_gadget_struck` with 2: within that many seconds of a gadget
    # the wearer commands taking a landed blow (the ruling: 2 seconds).
    "seconds_after_gadget_struck": (0.0, 60.0, "a number of seconds"),
    # "While your Fervour has no room to reserve another trap" (Overwhelming
    # Presence, as the passive order fixes of 2026-11-24 reword it) is
    # `no_room_for_a_trap`: a slot converted into a skill trap, and none of them
    # could reserve another. IT TAKES NO VALUE.
    "no_room_for_a_trap": None,
}

#: The figures of the Soul Collector's four Fervour thresholds, in order: "while
#: you hold 20 or more Fervour" opened at 25 points spent, then 40 at 50, 60 at
#: 100 and 100 at 200. The Death trees' slice C3d (2026-11-25).
#: `CataclysmFervourThresholds::Figure` holds the same four, and
#: `tools/tests/test_fervour_thresholds_are_the_vows.py` holds the two equal.
FERVOUR_THRESHOLDS = (20.0, 40.0, 60.0, 100.0)

#: The states a passive bonus's SIZE may grow with. Issue #968.
#:
#: A DIFFERENT QUESTION FROM `CONDITIONS` ABOVE. A condition decides whether the
#: bonus applies; a scale decides how large it is when it does. A row may carry
#: both, and neither implies anything about the other.
#:
#: HELD HERE AS WELL AS IN C++ ON PURPOSE, and the two have to agree.
#: `UCataclysmPassiveTree::AccumulateInto` turns each of these names into an
#: `ECataclysmStatScale`. A name it does not recognise is made worth NOTHING
#: rather than worth its full value, because the full value of a scaling bonus is
#: what a character in the most extreme state would get -- for Vicious Onslaught,
#: the bonus for being at death's door handed to one at full health. Refusing an
#: unknown name when the file is written is where it should be caught instead.
#:
#: EVERY SCALE HERE COUNTS WHOLE STEPS, ROUNDED DOWN, BUT ONE.
#: `share_of_range_moved`, the last entry, counts a FRACTION: its row is worth
#: its value times a share from 0 to 1 and nothing is rounded. Ruled
#: 2026-10-08. `SCALES_THAT_COUNT_A_FRACTION` below names it and says what a
#: row on it may not state.
#:
#: The value is (lowest step, highest step, what the step means).
SCALES = {
    # "for every 5% of your maximum health that is missing" is `health_missing`
    # with a step of 5. Steps are counted whole and rounded down, so a character
    # 12% below full health has two of them.
    "health_missing": (0.0, 100.0, "a percentage of maximum health"),

    # "for each point of Fervour you currently hold" is `class_resource_held`
    # with a step of 1. Issue #980. Steps are counted whole and rounded down the
    # same way, which cannot show at a step of one.
    #
    # NAMED FOR THE SHARED POOL RATHER THAN FOR FERVOUR. There is one class
    # resource and every class has it; Fervour is only the Masochist's name for
    # it, and a name from one tree would have to change the first time another
    # tree used the same shape. The upper bound is the largest maximum any class
    # line gives the pool.
    "class_resource_held": (0.0, 100.0, "a number of points of the pool"),

    # "for every 5% of your maximum health you currently owe" is `health_owed`
    # with a step of 5. Issue #994. The Reckoning reads the same state with a
    # step of 2. Steps are counted whole and rounded down, the same rule.
    #
    # OWED IS NOT MISSING, and the two are independent readings of health. A
    # character that deferred a cost owes health it is still standing on, so it
    # can be at full health and owe a fifth of it; one that paid the same cost
    # outright is a fifth down and owes nothing. Each has its own node.
    #
    # THE UPPER BOUND IS A STEP OF THE WHOLE POOL. What is OWED may pass a
    # character's maximum health -- that is what The Reckoning kills them for --
    # but a step larger than the pool would be a bonus nothing could ever reach.
    "health_owed": (0.0, 100.0, "a percentage of maximum health"),

    # "for every 1% of life leech you have" is `life_leech` with a step of 1.
    # Issue #1045. Glutton is the node and the only one.
    #
    # A READING OF A STAT AND NOT OF A STATE, which is what makes it unlike
    # every scale above. Those read where a character's health is or how full
    # its pool is, and change from moment to moment; life leech changes when the
    # character's gear or passive points change and not otherwise. So this is a
    # bonus that grows with an investment rather than with a situation.
    #
    # THE UPPER BOUND IS A STEP OF A HUNDRED PERCENT, which is a judgement and
    # not a figure read off anything: nothing in the design states a ceiling on
    # life leech. A hundred is the largest step that could still pay out for a
    # character that leeched every point of damage it dealt, and a step above
    # that would be a bonus nobody could ever reach.
    "life_leech": (0.0, 100.0, "a percentage of life leech"),

    # THREE COUNTS OF STACKS, one per kind. Issues #1002, #1003 and #1004.
    # "Each stack gives +1% increased attack speed per point" is
    # `momentum_stacks` with a step of 1.
    #
    # THREE NAMES RATHER THAN ONE NAME AND A COLUMN SAYING WHICH KIND. A fourth
    # column on this sheet is a row struct change, which means a build before
    # the DataTable asset can be regenerated and a column list to move; three
    # names cost three lines each and read the way the scales above do.
    #
    # THEY ARE NOT INTERCHANGEABLE. Each kind is granted by a different event
    # and lasts a different length of time -- 3, 5 and 8 seconds -- so a row
    # naming the wrong one would count somebody else's stacks and no arithmetic
    # would report it.
    #
    # THE UPPER BOUND IS THE LARGEST CAP ANY KIND HAS. A step larger than the
    # cap would be a bonus no character could ever reach.
    "momentum_stacks": (0.0, 10.0, "a number of stacks"),
    "bloodlust_stacks": (0.0, 10.0, "a number of stacks"),
    "carnage_stacks": (0.0, 10.0, "a number of stacks"),

    # "for each unique debuff on you" is `debuffs_carried` with a step of 1.
    # Issue #962. Four nodes read it and all four count single debuffs.
    #
    # NOT A FOURTH STACK COUNT, THOUGH IT IS COUNTED THE SAME WAY. A stack is
    # granted by an event this project chose to remember and expires on a timer
    # this project chose. A debuff is a gameplay effect somebody applied, and
    # the ability system is already holding the list for its own reasons.
    # `UCataclysmDebuffs` says which of those effects are harmful.
    #
    # THE UPPER BOUND IS A JUDGEMENT AND NOT A DESIGN RULE. Nothing caps how
    # many debuffs a character may carry -- issue #962's own notes say the
    # Vessel of Plagues keystone would need such a cap and there is none -- so
    # this is a sanity limit in the shape the stack counts use. Ten distinct
    # harmful effects at once is already far past anything the game can apply.
    "debuffs_carried": (0.0, 10.0, "a number of debuffs"),

    # "for each minion you have" is `minions_held` with a step of 1.
    # Issue #1518. The Ritualist's generator reads it: "1 per second for each
    # minion you have".
    #
    # EVERYTHING THE CHARACTER COMMANDS, WHICH IS WHAT THE TREE MEANS BY
    # "MINION": imps, thralls and deployed machines together. The decision of
    # 2026-09-08 kept "imp" and "thrall" in the ten places a node meant only
    # one of the two; the owner reversed it on 2026-09-16, and every Ritualist
    # node now says "minion". `UCataclysmCommand::ThingsCommandedBy` is the
    # list the game counts, and it is deliberately not `ThrallCountOf`, which
    # counts only the taken ones.
    #
    # COUNTED WHOLE THE WAY A DEBUFF AND A STACK ARE, because a minion is a
    # whole thing: there is nothing to divide and nothing to round.
    #
    # THE UPPER BOUND IS A JUDGEMENT IN THE SHAPE THE COUNTS ABOVE USE, on the
    # step a row states. What caps an army is the Fervour pool alone since
    # issue #2787 removed every count cap on what reserves it: a thrall
    # reserves 30 and an imp 10 against the Ritualist's 150. A step of ten
    # minions stays a sanity limit on what a row may state.
    "minions_held": (0.0, 10.0, "a number of minions"),
    # THE ENEMIES IN THE CHARACTER'S SIPHON. The Death trees' slice C2
    # (2026-11-25): Sapping Cold's and Stolen Strength's "for each of them, up
    # to 10 enemies", and Life Siphon's and Living Harvest's "each enemy in your
    # Siphon". Read from the character's own record, as `minions_held` is.
    "enemies_in_siphon": (0.0, 10.0, "a number of enemies"),

    # "for each Fervour threshold you are at or above" is
    # `fervour_thresholds_reached` with a step of 1: the Soul Collector's Steady
    # Count, the Death trees' slice C3d (2026-11-25). A count of the four, read
    # from the same answer `fervour_threshold_reached` reads.
    "fervour_thresholds_reached": (0.0, 4.0, "a number of Fervour thresholds"),

    # "for each enemy within 4 metres of you" is `enemies_in_reach` with a
    # step of 1. Issue #1597, and the third scale that is a count of things
    # after the debuffs carried and the minions held.
    #
    # COUNTED WHOLE THE WAY A DEBUFF AND A MINION ARE, because a creature
    # standing near you is a whole thing: there is nothing to divide and
    # nothing to round.
    #
    # THE STEP IS NOT THE RADIUS EITHER. Same column, same reason as the
    # condition above: `ScaleStep` holds how many enemies one step is worth,
    # so the distance needs `Reach Metres`, which is not built yet.
    #
    # THE COUNT ITSELF IS NOT CAPPED, RULED BY THE PROJECT OWNER ON
    # 2026-09-12 against a recommendation to cap it at ten. The design
    # document's rule is quoted in `docs/DECISIONS.md`: a multiplicative
    # source needs no cap because the product cannot reach immunity. THIS
    # BOUND IS ON THE STEP AND NOT ON THE COUNT, so it is no part of that
    # ruling. A step above ten enemies would be a bonus that almost never
    # moves, which is far likelier to be a mistake than a design.
    "enemies_in_reach": (0.0, 10.0, "a number of enemies"),

    # "Each Crippled enemy within 4 metres of you" is `crippled_enemies_in_reach`
    # with a step of 1. Issue #1515, for `Ravager_keystone_c_kC` Grinding Halt.
    #
    # THE SCALE ABOVE WITH ONE FILTER, and bounded the same way for the same
    # reason: the bound is on the STEP, not on the count, so the owner's ruling
    # that the count is uncapped is untouched. It cannot exceed the unfiltered
    # count, which is itself uncapped.
    "crippled_enemies_in_reach": (0.0, 10.0, "a number of crippled enemies"),

    # "For each enemy your attack hits beyond the first" is
    # `enemies_hit_beyond_the_first` with a step of 1. Issue #1515, for
    # `Ravager_basic_b_a2` Cleaving Arc.
    #
    # THE COUNT `enemies_hit_at_least` READS, LESS ONE: one enemy struck is
    # worth nothing and three are worth two. Bounded on the step as the reach
    # scales above are, and for the same reason.
    "enemies_hit_beyond_the_first": (0.0, 10.0, "a number of enemies beyond the first"),

    # "for every 2% of Damage Reduction you have" is `damage_reduction` with a
    # step of 2. Issue #1515, for `Ravager_capstone_100`'s second option,
    # Weight Against Them.
    #
    # A READING OF A STAT, AS `life_leech` IS: the attribute, holding what gear
    # and unconditional rows put there. A damage reduction row that holds only
    # in a situation does not raise it.
    #
    # THE READING STOPS AT THE 75% CAP, ruled on 2026-09-17, because "the
    # Damage Reduction you have" is the figure that reduces damage. So the
    # upper bound is the cap: a step above it could never pay.
    "damage_reduction": (0.0, 75.0, "a percentage of damage reduction"),

    # "for every full 200 maximum mana you have" is `max_mana` with a step of
    # 200. Issue #1515, for `Ritualist_basic_d_a2` Drawn Deep.
    #
    # THE MAXIMUM AND NOT THE MANA IN HAND, so spending mana does not shrink it.
    #
    # THE UPPER BOUND IS A JUDGEMENT IN THE SHAPE THE BOUNDS ABOVE USE. A
    # Ritualist has 1,278 maximum mana at level 100 before gear and passives
    # (90, and 12 a level after the first); a step past 1,000 would be a bonus
    # a character reaches once, if at all, which is far likelier a mistake.
    "max_mana": (0.0, 1000.0, "an amount of maximum mana"),

    # "Your Maximum Health also grants Armor: 1 Armor for every 10 maximum
    # health you have" is `Ravager_keystone_a_kC` Weight Bearing, where the step
    # is 10. Issue #1515.
    #
    # THE MAXIMUM AND NOT THE HEALTH IN HAND, so taking damage does not take the
    # armour away. The three health scales beside it read how FULL the bar is;
    # this one reads how BIG it is, and no arithmetic on a share recovers that.
    #
    # THE UPPER BOUND IS THE SAME JUDGEMENT THE ENTRY ABOVE MAKES, on measured
    # figures: `game/Data/ClassStats.csv` gives the Masochist the largest bar,
    # 150 with 24 a level after the first, so 2,526 at level 100 before gear and
    # passives, and the Ravager 2,110. A step past 1,000 would be a bonus worth
    # one or two of itself at the very end of a character's life, which is far
    # likelier a number typed into the wrong column.
    #
    # RAISED TO 5,000 ON 2026-11-24 for the War trees' rulings R2, R23 and R25,
    # which the owner accepted that day: Weight of the World, Dense Muscle and
    # Walking Fortress step by 5,000 maximum health. The figure above is before
    # gear and passives; the simulation's tier 8 reference character, level
    # 100 with half its slots on staying alive, holds 11,023
    # (`sim/cataclysm_sim/fight.py`, `reference_player`), and a Bulwark that
    # invests in health holds more, so a step of 5,000 is reached twice by that
    # character and more often by the build these nodes are for. The
    # coordinating session agreed the raise the same day.
    "max_health": (0.0, 5000.0, "an amount of maximum health"),

    # "Ranged skills deal 10%-20% bonus damage for each meter of distance to the
    # target" is `metres_to_target` with a step of 1. Issue #1981.
    #
    # THE SAME READING `target_within_metres` TESTS, SIZED RATHER THAN COMPARED.
    # That is the difference between a condition and a scale, and both are filled
    # only on the attacker's own lookups -- which is where a ranged skill's
    # damage is worked out.
    #
    # THE SAME 0 TO 100 METRE BOUND the two distance conditions use.
    "metres_to_target": (0.0, 100.0, "a number of metres"),

    # "All damage dealt is reduced by 15%-25% for each second you stand still" is
    # `seconds_stationary` with a step of 1. Issue #1981.
    #
    # THE READING `stationary_for_seconds` TESTS, SIZED RATHER THAN COMPARED.
    #
    # ITS ROW STATES NO CAP AND NONE IS INVENTED, because it does not need one:
    # the row is a MULTIPLYING reduction, and the pipeline floors one at -99%, so
    # one per cent of the hit survives however long the character stands there.
    # An increased row would have reached no damage at all. Measured and ruled on
    # 2026-09-18.
    #
    # THE SAME 0 TO 60 SECOND BOUND the seconds conditions use.
    "seconds_stationary": (0.0, 60.0, "a number of seconds"),

    # "You take 10%-20% increased damage for each second you have been in
    # combat, up to 10 stacks" is `seconds_in_combat` with a step of 1, and
    # "Your damage is reduced by 3%-5% for every second you spend out of combat,
    # up to 10 stacks" is `seconds_out_of_combat` with a step of 1. Issue #1815.
    # Each grants nothing on the other side of the line, so neither needs a
    # condition beside it.
    #
    # THE SAME 0 TO 60 SECOND BOUND the seconds conditions use.
    "seconds_in_combat": (0.0, 60.0, "a number of seconds"),
    "seconds_out_of_combat": (0.0, 60.0, "a number of seconds"),

    # "Each unique debuff on an enemy increases your crit chance against them by
    # 5%-10%" is `target_debuffs`, and "Each active buff on you increases your
    # damage by 5%-10%" is `buffs_held`, each with a step of 1. Issue #1815. The
    # same 0 to 10 bound the character's own debuff and minion counts use.
    "target_debuffs": (0.0, 10.0, "a number of debuffs"),
    "buffs_held": (0.0, 10.0, "a number of buffs"),

    # "Critical strikes grant a stack of power increasing all damage by 3%-5%
    # for 5 seconds, up to 5 stacks" is `own_stacks` with a step of 1: the
    # row's OWN stacks, granted by its Action Event, lasting its Stack
    # Seconds and capped by its Scale Max Steps. Issue #1833, ruled
    # 2026-09-23 under the owner's delegation: each row is its own stack.
    # A step of exactly one: a stack is a whole thing and the rows say "per
    # stack".
    "own_stacks": (1.0, 1.0, "one stack"),

    # "Each consecutive melee hit on the same enemy increases damage by 5%-10%
    # up to 8 stacks" is `consecutive_hits` with a step of 1: the row's own
    # count of hits in a row on one enemy, granted by `hit_dealt` and capped by
    # its Scale Max Steps. Issue #1833, phase 2, ruled 2026-09-24 under the
    # owner's delegation. It reads nought on any other enemy, so hit N deals
    # damage with N-1. No timer: a hit on another enemy, death and leaving
    # combat end it, so it takes no Stack Seconds.
    "consecutive_hits": (1.0, 1.0, "one hit"),

    # "for each second the enemy has stayed poisoned by you without a break, up
    # to 10 seconds" is `seconds_poisoned_by_you` with a step of 1 and a Scale
    # Max Steps of 10: the Shadow's Festering, the Death trees' slice C6b4
    # (2026-11-25). Whole seconds, read off the record kept on the creature
    # asked about; none for one not carrying the asker's Poison.
    "seconds_poisoned_by_you": (1.0, 1.0, "one second"),

    # "Each Fervour above it gives 1% increased Attack Speed for 5 seconds, up to
    # 30%" is `fervour_overflow_stacks` with a step of 1 and a Scale Max Steps of
    # 30: the Soul Collector's Overflow, the Death trees' slice C6c (2026-11-25).
    # One stack for each whole Fervour a gain could not fit above the full bar,
    # their shared window `fervour_overflow_attack_speed_seconds` long.
    "fervour_overflow_stacks": (1.0, 1.0, "one Fervour above the bar"),

    # "Take 5%-15% more damage per active aura" is `auras_held`, with a step of
    # 1. Issue #1686. Every aura skill running, however many can run at once;
    # two can today. The same 0 to 10 bound as the buff count above.
    "auras_held": (0.0, 10.0, "a number of auras"),

    # "Your skills deal 1.5%-2.5% less damage for every 10 class points spent
    # above 100" is `class_points_spent` with a step of 10 and a Scale Offset of
    # 100. Issue #1686. A class point is a passive point spent, ruled under the
    # owner's delegation on 2026-09-23. Bounded by the 230 point budget.
    "class_points_spent": (0.0, 230.0, "a number of class points"),

    # "Your skills deal 10%-30% of your current mana as more damage" is
    # `mana_held_percent`, flat 10 to 30 with a step of 1: the value is a
    # PERCENTAGE of the mana held, the one scale that reads its value that way,
    # so the row carries the sentence's own range. Issue #1815. The same 0 to
    # 1,000 bound as `max_mana`.
    "mana_held_percent": (0.0, 1000.0, "an amount of mana"),

    # "... for every 1000 enemies killed this run" is `run_kills` with a step of
    # 1000, and "You lose 1-4% max resistances for every 100,000 - 500,000
    # kills" is `character_kills`. Issue #1833, the kill counter. A kill is the
    # character's own "kill" event, and "this run" is since the session began.
    # Bounded at a million: a campaign reaches about 335,500 creatures if every
    # one dies (sim/analyse_experience_curve.py, 257.5 a floor, 50.1 floors,
    # 26 dungeons), and the widest stated step is 500,000.
    "run_kills": (0.0, 1_000_000.0, "a number of kills this run"),
    "character_kills": (0.0, 1_000_000.0, "a number of kills in every run"),
    # "This weapon has 5-20% more damage for every 100,000-500,000 kills" is
    # `weapon_kills`: the kills made with the item the row is on, counted on
    # both worn weapons. Issue #1833, ruled 2026-09-25. The same bound.
    "weapon_kills": (0.0, 1_000_000.0, "a number of kills with the weapon"),
    # "Gadgets deal 5%-10% increased damage for each second they have been
    # active, up to 30 seconds" and "Gadgets that survive for 15 seconds gain a
    # permanent 20%-40% damage bonus" are `minion_seconds_active`, read on a
    # deployable's blow. Issue #1833, deployable Part 2. The Spear ultimate's
    # machines last 20 seconds, the longest today.
    "minion_seconds_active": (0.0, 60.0, "a number of seconds a minion has been active"),
    # "Each active gadget increases your evasion chance by 5%-10%" is
    # `deployables_active`, the machines the character commands now. Issue
    # #1833, deployable Part 3.
    "deployables_active": (0.0, 20.0, "a number of deployable machines"),
    # `traps_active` and `gadgets_active`, ruled 2026-10-07 and built ahead of
    # their rows. A trap is a deployable machine whose type also carries
    # `Type.Trap`; a gadget, for this scale, is a deployable machine that does
    # not. `deployables_active` above is unchanged and counts both. "Traps
    # deal 15%-30% increased damage for each other trap currently active on
    # the battlefield" is `traps_active` with a step of 1 and a Scale Offset
    # of 1; "Each active gadget increases trap damage by 10%-20%" is
    # `gadgets_active` with a step of 1. The same bound as the machines.
    "traps_active": (0.0, 20.0, "a number of traps"),
    "gadgets_active": (0.0, 20.0, "a number of gadgets, the machines that are not traps"),
    # Issue #1833 group C part 3c. "for every 15 seconds spent on the same
    # dungeon floor" is `seconds_on_floor` with a step of 15; "for every dungeon
    # floor cleared this run" is `floors_cleared` with a step of 1; "for every
    # 100 points of armor you have" is `armor` with a step of 100, the Armor
    # attribute; "For every unique Cataclysm boss defeated" is
    # `cataclysm_bosses_defeated` with a step of 1.
    "seconds_on_floor": (0.0, 3600.0, "a number of seconds on one floor"),
    "floors_cleared": (0.0, 1000.0, "a number of dungeon floors cleared"),
    "armor": (0.0, 100_000.0, "an amount of armor"),
    "cataclysm_bosses_defeated": (0.0, 100.0, "a number of unique Cataclysm bosses"),

    # "for each active unique instance of leech" is `leech_pools_in_flight` with
    # a step of 1. Issue #1833, ruled 2026-10-07 for Starvation's six-piece
    # bonus: one for each POOL the character is leeching into, health, mana and
    # energy shield, so nought to three. Not one for each payment, which every
    # hit starts and which has no bound.
    "leech_pools_in_flight": (0.0, 3.0, "a number of pools being leeched into"),

    # "While leeching, reduce your max resistances by 1%-3% per second" is
    # `seconds_leeching` with a step of 1. Ruled 2026-10-07: whole seconds of
    # unbroken leeching, nought again when no payment is owed. The ruling caps
    # the sentence at 10 seconds, a judged number the sentence does not state,
    # and the row carries it as a Scale Max Steps of 10; the scale itself has
    # no cap, so a row that states none is refused
    # (`SCALES_THAT_NEED_A_CAP`).
    #
    # THE SAME 0 TO 60 SECOND BOUND the seconds conditions use.
    "seconds_leeching": (0.0, 60.0, "a number of seconds"),

    # "Charge skills deal 30%-60% bonus damage proportional to distance
    # traveled" is `share_of_range_moved` with a step of 1. Ruled 2026-10-08
    # under the owner's delegation: the row's value is the bonus AT THE SKILL'S
    # FULL RANGE and falls in proportion below it, so a 14 metre charge that
    # has moved 7 metres gets half; capped at the whole value.
    #
    # THE ONE SCALE WHOSE COUNT IS A FRACTION. Every other entry counts whole
    # steps. There is no distance per step, so THE STEP IS 1 AND NOTHING ELSE:
    # the whole range is worth one value. It is nought for every blow that is
    # not a charge's own, so the row requires `Keyword.Charge` only to say
    # which skills it is about.
    "share_of_range_moved": (1.0, 1.0, "the whole of a charge's range"),

    # "This bonus scales with the number of enemies in the area", Warlord's
    # Will's ten-piece bonus, is `enemies_in_reach_while_own_stack` with a step
    # of 1. Ruled by the project owner on 2026-10-17: +10% more damage per
    # enemy within 8 m, capped at 5 enemies, for the 5 s the bonus lasts. The
    # count `enemies_in_reach` takes, and nothing outside the window that an
    # `own_stacks` row of the same enchantment on the same stat opens; this row
    # grants no stack of its own. The 8 m is the loader's,
    # `UCataclysmItemModifiers::OwnStackWindowReachMetres`, because this sheet
    # has no reach column. A step of exactly one enemy. The cap and the
    # sibling are required by `refuse_a_window_scale_with_no_own_stack_row`,
    # and `passive_effects` refuses the scale outright: a passive node has no
    # own stacks.
    "enemies_in_reach_while_own_stack": (1.0, 1.0, "one enemy"),

    # "you gain 1% increased Attack Speed for every 2% increased Movement Speed
    # you have" is `increased_movement_speed` with a step of 2. The Ravager tree
    # rework of 2026-11-24, Relentless. The INCREASES bucket of the character's
    # own `movement_speed` line, in percentage points, judged with its state;
    # `ECataclysmStatScale::PerPercentOfIncreasedMovementSpeed` says how it is
    # read. Whole steps, rounded down. Bounded at 1,000, the bound the other
    # percentage-of-a-stat scales take.
    "increased_movement_speed": (0.0, 1000.0, "a percentage of increased movement speed"),

    # THE WAR VOW OPTIONS' SECOND SLICE, 2026-11-25, on the coordinating
    # session's rulings that day. Titan's "Your maximum HP is increased by 1%
    # for every 2 points of maximum Fervour you have" is `max_class_resource`
    # with a step of 2, and Unbridled's "Every 20 points of maximum Fervour you
    # have" a step of 20: the maximum `MaximumClassResource` answers, with the
    # `class_resource` rows that hold now counted, so Ragnarok's doubling raises
    # Titan's health as ruled. Bounded at 1,000 points, the bound
    # `class_resource_points_at_least` takes. A `class_resource` row may not
    # scale by it (`MAXIMUM_CLASS_RESOURCE_SCALE`). Bastion's "Your Maximum HP
    # is increased by 1% for every 1% Block Chance you have" is `block_chance`
    # with a step of 1: the Block Chance attribute in percentage points, the
    # character sheet's figure, as ruled, and not the chance a blow is judged
    # against. Whole steps, rounded down, for both.
    "max_class_resource": (0.0, 1000.0, "a number of points of the class resource's maximum"),
    "block_chance": (0.0, 100.0, "a percentage of block chance"),

    # THE WAR VOW OPTIONS' THIRD SLICE, SECOND HALF, V3b, 2026-11-25. Colossus
    # Eternal's "your attacks deal bonus damage equal to 2% of your current HP"
    # is a flat `attack_damage` of 2 with a step of 100 points of current health:
    # the share of maximum health held times the maximum, as the character's
    # state reads them. Whole steps, rounded down. Bounded as `max_health` is.
    "current_health": (0.0, 5000.0, "an amount of current health"),

    # THE FINISHING OF THE DEATH CATACLYSM, SLICE C, 2026-11-26. Close Packed's
    # "for each other minion of yours within 3 metres of it, up to 5" is
    # `minions_near_the_minion` with a step of 1, a `Reach Metres` of 3 and a cap
    # of 5: the summoner's OTHER followers (raised minions and thralls, not
    # machines) within the row's reach of the minion whose figure is asked,
    # which `UCataclysmAbilitySystemComponent::MultiplierForStatAgainst` measures
    # when a minion is named. With no minion named it counts nothing, so only a
    # minion stat read with the minion asking can use it: today
    # `minion_detonation_damage`. Bounded as `minions_held` is.
    "minions_near_the_minion": (0.0, 10.0, "a number of other minions"),
}

#: The scale that reads the class resource's maximum with its rows counted, which
#: a `class_resource` row may not take. The War vow options' second slice,
#: 2026-11-25. The engine reads that maximum by evaluating the `class_resource`
#: line without the readings a row must ask for, so a row on that line scaled by
#: it would read an unknown maximum and grant nothing.
MAXIMUM_CLASS_RESOURCE_SCALE = "max_class_resource"


#: The largest cap on a scale a row may state, in whole steps. Issue #1815.
#: The largest the sentences state today is 10 ("up to 10 stacks"); 100 leaves
#: room and still refuses a number typed into the wrong column.
MAX_SCALE_STEPS = 100


#: The longest a row's own stacks may last, in seconds. Issue #1833. The
#: sentences state 2 to 5; the same 60 second sanity bound the clocks use.
#:
#: WHAT THE STACK SECONDS COLUMN HOLDS, BY ROW. How long an own stack lasts,
#: on a stat row scaled by `own_stacks`; the window of a placed stack, on
#: `PLACED_ACTIONS`; the window a nearby action's Every Nth counts in, on
#: `NEARBY_ACTIONS`; and, since 2026-10-08, HOW LONG A STUN LASTS on
#: `STUN_NEAR_THE_DYING_ACTION`, where it is bounded by `MAX_STATUS_SECONDS`
#: and not by this. That last is a duration with no stack behind it: a new
#: use of the column, ruled 2026-10-08, because the row's value is its chance.
MAX_STACK_SECONDS = 60.0


#: Enchantments whose row is the COMPLEMENT of the range their sentence states:
#: each end of the row is 100 minus the sentence's number at the same place,
#: with a minus sign. Issue #1833, the rows-only batch, ruled 2026-09-25 under
#: the owner's delegation.
#:
#: "Your maximum HP cannot exceed 40%-60% of its normal value" is max_health
#: more -60 to -40. The item text and the value are both taken by POSITION in
#: the range (`UCataclysmItemValues::EnchantmentTextAtRoll` and
#: `EnchantmentValue`), so at the lowest roll the text reads 40% and the value
#: -60 leaves 40% of the maximum; at the highest, 60% and -40. The two agree at
#: every roll.
#:
#: KEYED BY ENCHANTMENT AND NOT BY STAT, because "You have 20% less hp" is a
#: more row on the same stat and its sentence states its own number.
#: `tools/tests/test_enchantment_effects_match_the_row_text.py` reads this set
#: and checks that every name in it is still needed.
COMPLEMENT_RANGE_ENCHANTMENTS = frozenset({
    "Negative_Your_maximum_HP_cannot_exceed_40_60_of_its_nor",
    # AND THE CRIT CEILING, issue #1833, 2026-09-30: max_crit_chance flat
    # -70 to -50 over the attribute's 100.
    "Negative_Your_critical_strike_chance_cannot_exceed_30_50",
})

#: Enchantments whose sentence states its range as the stat's BASE PLUS the
#: row's value, and that base. Issue #1833 group E part 2, ruled 2026-10-02:
#: "You block for 65%-75% of damage instead of the normal 50%" is
#: `block_damage_reduction` flat 15 to 25 on the base of 50 that
#: `UCataclysmPlayerClassStats::EngineSuppliedBases` states. The item text and
#: the value are both taken by position in the range, so at the lowest roll the
#: text reads 65% and the value 15 makes 65; the two agree at every roll, as a
#: complement range's do. Keyed by enchantment, for the reason the complement
#: set gives. `tools/tests/test_enchantment_effects_match_the_row_text.py`
#: checks that every name here is still needed.
#:
BASE_PLUS_RANGE_ENCHANTMENTS: dict[str, float] = {
    "Positive_You_block_for_65_75_of_damage_instead_of_the_n": 50.0,
}

#: The scales that read a Scale Offset: how much of the reading is not counted.
#: Issue #1686. The class point sentences state a threshold ("above 100",
#: "above 50"), and the engine's `ValidateModifier` refuses an offset on a
#: scale that does not read one, so the generator refuses it first, with the
#: row named.
#:
#: AND `traps_active`, SINCE 2026-10-07. "For each other trap" is an offset of
#: 1: the row is read on a trap's blow, and the trap striking is not another
#: trap. `MAX_SCALE_OFFSET` below is the class point budget and is not
#: narrowed for it; an offset above the 20 traps the scale is bounded at
#: counts nothing.
SCALES_THAT_TAKE_AN_OFFSET = frozenset({"class_points_spent", "traps_active"})

#: The largest offset a row may state: the 230 point budget. Issue #1686.
MAX_SCALE_OFFSET = 230

#: Scales whose row must state a cap in Scale Max Steps. Ruled 2026-10-07.
#: `seconds_leeching` counts for as long as the wearer goes on leeching, and
#: its sentence, "While leeching, reduce your max resistances by 1%-3% per
#: second", states no end. The ruling caps it at 10 seconds; the engine's
#: scale holds no cap of its own, so a row written without one would be
#: UNCAPPED. The row is refused instead. The Passive Effects sheet has carried
#: the column since 2026-11-24, so a row there naming one of these is refused
#: unless it states a cap, as an enchantment's is.
SCALES_THAT_NEED_A_CAP = frozenset({"seconds_leeching"})

#: The scales whose count is a FRACTION and not a number of whole steps. Ruled
#: 2026-10-08. EVERY OTHER SCALE COUNTS WHOLE STEPS, ROUNDED DOWN.
#: `share_of_range_moved` is the metres a charge had moved when its blow
#: landed over the range the skill used, from 0 to 1, and its row is worth the
#: value times that.
#:
#: A ROW ON ONE MAY STATE NEITHER A CAP NOR AN OFFSET. `Scale Max Steps` caps a
#: number of whole steps, and there are none: the scale holds its own cap, at
#: the whole value. `Scale Offset` is how much of a reading is not counted,
#: and no sentence leaves part of a charge uncounted. The cap is refused where
#: `Scale Max Steps` is read; the offset is refused already, because the scale
#: is not in `SCALES_THAT_TAKE_AN_OFFSET`. The engine's `ValidateModifier`
#: refuses both as well.
SCALES_THAT_COUNT_A_FRACTION = frozenset({"share_of_range_moved"})

#: The stats whose asker hands over how far a charge went, which is what
#: `share_of_range_moved` reads. Ruled 2026-10-08. The two figures travel on
#: the blow (`FCataclysmHitDelivery::MetresCharged` and `ChargeRangeMetres`)
#: and `UCataclysmSkillEffects::ApplyHit` passes them to the lookups of these
#: two stats and to no other. On any other stat the scale reads nought every
#: time, so the row would be accepted and dead; `refuse_a_scale_nothing_asks_for`
#: refuses it.
STATS_ASKED_WITH_A_CHARGES_DISTANCE = frozenset({"attack_damage", "spell_damage"})


#: The value kinds a stat row may carry, on both sheets that write one.
#:
#: THE THREE BUCKETS OF THE STAT PIPELINE, AND A REMOVAL. Issue #1791. A removal
#: takes its stat to nothing whatever else reaches it:
#: `UCataclysmStatPipeline::Evaluate` multiplies the finished figure by zero,
#: which is the project owner's mechanic of 2026-09-16 for sentences such as
#: "You have no armor". A removal row states 1 and nothing reads the number.
#:
#: ONE TUPLE FOR BOTH SHEETS. The Passive Effects and Enchantment Effects readers
#: each spelled out the three buckets until then, so a fourth added to one of
#: them only would be a kind one sheet writes and the other refuses.
#:
#: THE GAME READS THESE NAMES IN TWO PLACES, `EnchantmentModifierFor` in
#: `CataclysmItem.cpp` and `UCataclysmPassiveTree::AccumulateInto`, and both send
#: a name they have no case for to the increased bucket. A name added here and
#: not there would be written as an increase of its value, which is why
#: `tools/tests/test_value_kinds_match_the_engine.py` holds the three lists to one.
VALUE_KINDS = ("flat", "increased", "more", "removed")

#: Stats a row may only remove, because the code reading them reads nothing else.
#:
#: `mana_on_hit` IS THE BASIC ATTACK'S OWN FIGURE from `game/Data/SkillSlots.csv`
#: rather than a stat. Issue #1791. `UCataclysmSkillTemplate::ApplyManaOnHit` asks
#: `UCataclysmAbilitySystemComponent::IsStatRemoved` for it and asks nothing more,
#: so a flat, increased or more row on it would be written, recorded on the
#: character, and read by nothing -- the silent failure issue #1025 describes.
#:
#: EVERY NAME HERE MUST BE ON THE ENGINE'S LIST OF STATS WITH NO ATTRIBUTE, or
#: the character would never record the removal at all.
#: `test_every_removal_only_stat_is_one_the_engine_records` holds that.
REMOVAL_ONLY_STATS = frozenset({"mana_on_hit"})


def _check_value_kind(sheet: str, index: int, who: str, stat: str,
                      kind: str) -> None:
    """A stat row's value kind is one the game reads, on a stat it can apply to.

    REFUSED RATHER THAN IGNORED, for the reason `_check_pool_action` gives: a
    row that validates and then grants nothing is silent everywhere else.

    ANYTHING BUT A REMOVAL ON A STAT THAT MAY ONLY BE REMOVED IS REFUSED, for the
    reason `REMOVAL_ONLY_STATS` gives.

    A REMOVAL ON COOLDOWN REDUCTION IS NOT REFUSED. Issue #1791, ruled 2026-09-16
    under the project owner's delegation. It means "you have no cooldown
    reduction": the stat's attribute is written through the same pipeline and
    taken to nothing, so every cooldown runs at its base length.
    """
    if kind not in VALUE_KINDS:
        raise DataError(
            f"{sheet} row {index}: {who} has value kind {kind!r}, which is not "
            f"{', '.join(VALUE_KINDS[:-1])} or {VALUE_KINDS[-1]}")

    if stat in REMOVAL_ONLY_STATS and kind != "removed":
        raise DataError(
            f"{sheet} row {index}: {who} gives {stat!r} the value kind {kind!r}. "
            f"That stat may only be removed: the code reading it asks whether "
            f"it is removed and nothing else, so the row would grant nothing.")


#: The Enchantment Effects columns a Passive Effects row may state since the
#: passive row port of 2026-11-24 (`plan_death_and_war.md`, slice B28): what a
#: row DOES when an event happens, its own stacks, a second condition, an offset
#: on its scale, and the rest of that sheet's action columns. A row stating any
#: of them is read by `_effect_row`, which holds the Enchantment Effects sheet's
#: own per-row rules, and the game builds it with the enchantment rows' builder
#: (`UCataclysmItemModifiers::AccumulateEnchantmentsInto`), so a passive action
#: and a worn one cannot be judged by two sets of rules.
PASSIVE_ACTION_COLUMNS = (
    "Action", "Action Event", "Fraction Of", "Stack Seconds", "Scale Offset",
    "Every Seconds", "Every Nth", "Condition 2", "Condition Value 2",
    "Trigger Cooldown", "Event Value", "Ailment", "Damage Share")

#: The scales whose count an action grants: a row's own stacks, and its hits in
#: a row on one enemy. A passive row naming one is read the action way too,
#: because the stacks need the action that grants them, which only the
#: enchantment rows' builder makes. `UCataclysmPassiveTree::
#: IsBuiltAsAnEnchantmentRow` names the same two.
SCALES_AN_ACTION_GRANTS = frozenset({"own_stacks", "consecutive_hits"})

#: The Enchantment Effects columns a Passive Effects row may never state. Each
#: is about an item: its name and words, and the roll that picks a value inside
#: a range. A passive row names its node, is paid per point and rolls nothing.
ENCHANTMENT_ONLY_COLUMNS = (
    "Enchantment", "Effect", "Value Low", "Value High", "Scale Step High",
    "Stack Seconds High", "Condition Value High")

#: The fields a Passive Effects row carries for the action columns, in the
#: order `game/Data/PassiveEffects.csv` writes them, with the value a row
#: stating none of them carries: what `_effect_row` writes for an empty cell.
#: `FCataclysmPassiveEffectRow` holds the same thirteen, after `ScaleMaxSteps`,
#: and `PerPointFrom` after them.
PASSIVE_PORT_DEFAULTS = (
    ("Action", ""), ("ActionEvent", ""), ("FractionOf", ""),
    ("StackSeconds", 0.0), ("ScaleOffset", 0.0), ("EverySeconds", 0.0),
    ("EveryNth", 0), ("Condition2", ""), ("ConditionValue2", 0.0),
    ("TriggerCooldown", 0.0), ("EventValue", 0.0), ("Ailment", ""),
    ("DamageShare", 0.0))
PASSIVE_PORT_FIELDS = tuple(field for field, _ in PASSIVE_PORT_DEFAULTS)

#: The names the passive sheet's reach rule counts within a radius. Issue
#: #1597, and the crippled-enemy scale of issue #1515.
PASSIVE_REACH_NAMES = frozenset({"enemies_in_reach_at_least", "enemies_in_reach",
                                 "crippled_enemies_in_reach",
                                 # AND THE DEATH TREES' SLICE C6b3'S TWO.
                                 "enemy_of_rarity_in_reach", "no_enemy_in_reach",
                                 # AND SLICE C6d1'S OWN MINIONS.
                                 "minions_in_reach_at_least"})


def _chance_percent(raw, headers, index: int, node: str, action: str) -> float:
    """The Chance Percent column: the chance per point that a class resource row pays, or 0 for every time.

    THE DEATH TREES' SLICE C6b3 (2026-11-25), ruled for Tear Loose, "a 2% chance per point to give you 1 Fervour":
    the whole-numbers rule ruled out paying the average, 0.02 a point. A ROW WITH A CHANCE IS PAID PER POINT IN ITS
    CHANCE: `Value Per Point` is the amount each success pays, and the game multiplies the chance by the points. An
    empty cell is every time, which is every row written before the column, and a sheet without the column reads as
    empty. Refused on a row whose Action is not class_resource, and outside above 0 to 100: a 0 written would be read
    as every time, the opposite of what it says.
    """
    if "Chance Percent" not in headers:
        return 0.0
    written = clean(_cell(raw, headers, "Chance Percent"))
    if not written:
        return 0.0
    value = number(written, "Chance Percent", index)
    # AND TWICE ANSWERED'S ACTION, the Death trees' slice C6b4 (2026-11-25), as
    # ruled. Nothing else reads a chance.
    if action not in (CLASS_RESOURCE_POOL, NEXT_HIT_CRITICAL_TWICE_ACTION):
        raise DataError(
            f"Passive Effects row {index}: {node} states a Chance Percent of "
            f"{written!r} on {'the action ' + repr(action) if action else 'a stat row'}. "
            f"A chance is read by a {CLASS_RESOURCE_POOL!r} row and the action "
            f"{NEXT_HIT_CRITICAL_TWICE_ACTION!r} only.")
    if not 0.0 < value <= 100.0:
        raise DataError(
            f"Passive Effects row {index}: {node} has a Chance Percent of "
            f"{value}, and a chance is above 0 and up to 100. Leave the column "
            f"empty for every time.")
    return value


def _passive_row_is_action_shaped(raw, headers) -> bool:
    """Whether a Passive Effects row is read by the Enchantment Effects rules.

    A ROW STATING ANY ACTION COLUMN, OR SCALED BY A COUNT AN ACTION GRANTS. The
    passive row port of 2026-11-24. Every other row is read as it always was.
    """
    if any(clean(_cell(raw, headers, column)) for column in PASSIVE_ACTION_COLUMNS):
        return True
    return clean(_cell(raw, headers, "Scale")) in SCALES_AN_ACTION_GRANTS


def _passive_action_row(raw, headers, index: int, node: str,
                        sentence: str) -> dict:
    """A Passive Effects row stating action columns, read by `_effect_row`.

    THE PASSIVE SHEET'S NAMES ARE MAPPED ONTO THE ENCHANTMENT SHEET'S, and
    nothing else is changed: `Node` is read where `Enchantment` would be, and
    `Value Per Point` where `Value Low` would be. A plain dict rather than the
    sheet's own header map, so a column this sheet does not have reads as empty
    in that function, as `OPTIONAL_COLUMNS` already allows the action columns
    to here.

    `sentence` IS THE NODE'S OWN WORDS, or the option's for a capstone option
    row, so that a rule reading a row's words (the name of its kept stacks)
    reads the words the player is shown.

    A REFUSAL IS REPORTED AGAINST THIS SHEET. `_effect_row` names the
    Enchantment Effects sheet in every message it raises, and the row number it
    gives is this sheet's, so the sheet's name is corrected and the rest of the
    message left as it was.
    """
    aliased = dict(headers)
    aliased["Enchantment"] = headers["Node"]
    aliased["Value Low"] = headers["Value Per Point"]
    try:
        return _effect_row(raw, aliased, index, node, {node: sentence},
                           {node: ""}, {node: frozenset()},
                           sheet_repeats_the_words=False)
    except DataError as error:
        raise DataError(str(error).replace(
            "Enchantment Effects row", "Passive Effects row", 1)) from error


def _passive_node_sentences() -> dict[tuple[str, int], str]:
    """Each node's words, and each capstone option's, by node and option number.

    0 IS THE NODE'S OWN SENTENCE; 1 TO 3 A CAPSTONE'S OPTIONS. Read from the
    tree files by `passive_nodes`, which is what the game shows.
    """
    sentences: dict[tuple[str, int], str] = {}
    for row in passive_nodes():
        sentences[(row["Name"], 0)] = row["Description"]
        for option in range(1, CAPSTONE_OPTIONS + 1):
            sentences[(row["Name"], option)] = row[f"Option{option}Description"]
    return sentences


#: The words a capstone's own sentence opens with to offer its choice, and
#: nothing more: "Unlocks at 25 points spent. Choose one. The choice is
#: permanent." What follows them, if anything, is a clause the capstone grants
#: whichever option is chosen.
CAPSTONE_CHOICE_WORDS = re.compile(
    r"^\s*Unlocks at \d+ points spent\.\s*Choose one\.\s*The choice is permanent\.\s*")


def capstone_states_a_clause(sentence: str) -> bool:
    """Whether a capstone's own sentence states a clause beside its choice.

    THE COORDINATING SESSION'S RULING 4 OF 2026-11-25 (the Death trees): a row
    with no option on a capstone applies whatever is chosen, and is accepted only
    on a capstone whose own sentence states that clause -- the Soul Collector's
    four vows, "Reaching 25 points also opens the first Fervour threshold: while
    you hold 20 or more Fervour, all your skills have 20% more Area of Effect".
    A capstone whose sentence says only its choice still refuses one.
    """
    return bool(CAPSTONE_CHOICE_WORDS.sub("", sentence, count=1).strip()) \
        and bool(CAPSTONE_CHOICE_WORDS.match(sentence))


def _passive_option(raw, headers, index: int, node: str) -> int:
    """Which of a capstone's three options a Passive Effects row belongs to.

    Issue #1029. Empty is "not an option", which is every row in the trees
    except the capstones'.

    A COLUMN AND NOT THE `Condition` COLUMN, and the reason is concrete rather
    than stylistic. A row may need an option AND a real condition at once: the
    Second Vow's second option reads "Dropping below 50% health grants immunity
    to all damage for 5 seconds", which is option 2 and a health trigger
    together, and one column cannot hold both.

    WHICH NODES MAY CARRY ONE IS CHECKED IN `validate_passive_effects`, because
    that is where the node table is in hand and its `Kind` column says which
    nodes are capstones. Moved out of `passive_effects` unchanged on 2026-11-24,
    so its action rows read it by the same rule.
    """
    option = 0
    if clean(_cell(raw, headers, "Option")):
        option = int(number(_cell(raw, headers, "Option"), "Option", index))
        if not 1 <= option <= CAPSTONE_OPTIONS:
            raise DataError(
                f"Passive Effects row {index}: {node} names option "
                f"{option}, and a capstone offers {CAPSTONE_OPTIONS}. Leave "
                f"the column empty for a row that is not a capstone option.")
    return option


def _passive_min_points(raw, headers, index: int, node: str) -> int:
    """From how many points in its own node a Passive Effects row applies.

    Issue #1755. Empty is 0, which is every row before the column: the row
    applies from the first point and is paid per point. Above 0 is a node
    sentence's "At 4 points: ..." clause, which the node grants whole from that
    point on and never multiplies by the points, unless the row's Per Point From
    says otherwise (`_per_point_from`). Moved out of `passive_effects`
    unchanged on 2026-11-24, so its action rows read it by the same rule.
    """
    min_points = 0
    if clean(_cell(raw, headers, "Min Points")):
        written = number(_cell(raw, headers, "Min Points"), "Min Points", index)
        if written != int(written) or written < 1:
            raise DataError(
                f"Passive Effects row {index}: {node} has a Min Points of "
                f"{written}, and it takes a whole number of points from 1 "
                f"up. Leave the column empty for a row paid per point.")
        min_points = int(written)
    return min_points


def _per_point_from(raw, headers, index: int, node: str, min_points: int) -> int:
    """The Per Point From column: from which point a threshold row is paid per point.

    THE PASSIVE ROW PORT OF 2026-11-24, for "per point from N points". Empty is
    0, which keeps the rule of issue #1755: a row with Min Points applies from
    that many points and once. A number N from 1 up makes it pay its value once
    for every point from the Nth on, so Tempered Steel's "At 6 points: These
    bonuses are doubled while at maximum Fervour" is Min Points 6 and Per Point
    From 1 (all of its points), and Tinkerer's "At 6 points: Gadgets remain
    active 20% longer per point invested beyond 6" is Min Points 7 and Per Point
    From 7 (the seventh point and the eighth).

    IT NEEDS A THRESHOLD AND CANNOT START ABOVE IT. With no Min Points a row is
    already paid per point from the first, so the column would change nothing;
    above the threshold, the row would apply at that threshold and count no
    points there, which is a row worth nothing at the point it switches on.
    """
    written = clean(_cell(raw, headers, "Per Point From"))
    if not written:
        return 0
    value = number(written, "Per Point From", index)
    if value != int(value) or value < 1:
        raise DataError(
            f"Passive Effects row {index}: {node} has a Per Point From of "
            f"{value:g}, and it takes a whole number of points from 1 up. Leave "
            f"the column empty for a row paid once from its Min Points.")
    if not min_points:
        raise DataError(
            f"Passive Effects row {index}: {node} has a Per Point From of "
            f"{int(value)} and no Min Points. A row with no threshold is paid "
            f"per point from its first point already, so the column would "
            f"change nothing.")
    if value > min_points:
        raise DataError(
            f"Passive Effects row {index}: {node} is paid per point from point "
            f"{int(value)} and applies from {min_points} points, so at {min_points} "
            f"points it would apply and count no points. Per Point From is at "
            f"most the Min Points.")
    return int(value)


def _max_points(raw, headers, index: int, node: str, min_points: int) -> int:
    """The Max Points column: the number of points from which a row stops applying.

    THE SABOTEUR'S ROWS OF 2026-11-25, on the coordinating session's ruling for
    Durable Modifications: its 3% a point above 50% Fervour holds at 1 to 3
    points and its 6% a point from 4, so the two never multiply together.
    EXCLUSIVE: 4 applies at 3 points and not at 4. AN EMPTY CELL IS NO LIMIT,
    which is every row a script that does not know the column writes.

    A WHOLE NUMBER FROM 2 UP, AND ABOVE ITS MIN POINTS: at or below the
    threshold the row would never apply.
    """
    written = clean(_cell(raw, headers, "Max Points"))
    if not written:
        return 0
    value = number(written, "Max Points", index)
    if value != int(value) or value < 2:
        raise DataError(
            f"Passive Effects row {index}: {node} has a Max Points of {value:g}, "
            f"and it takes a whole number of points from 2 up, the point at which "
            f"the row stops. Leave the column empty for no upper limit.")
    if min_points and value <= min_points:
        raise DataError(
            f"Passive Effects row {index}: {node} applies from {min_points} points "
            f"and stops at {int(value)}, so it never applies. Max Points is above "
            f"the Min Points.")
    return int(value)


def passive_effects(book) -> list[dict]:
    """What a passive node grants, one stat effect per row.

    A NODE SAYS WHAT IT DOES IN ENGLISH AND THIS SAYS IT IN NUMBERS. The four
    class tree files carry a sentence written for a player -- "+2% increased
    Life Leech per point" -- and no stat name, no bucket and no machine-readable
    value. This sheet is where those are authored, which the project owner chose
    on 2026-08-25 over changing the separate tree authoring tool's schema.

    IT COVERS A SMALL PART OF THE TREE AND THAT IS THE HONEST POSITION. Most
    nodes are not stat modifiers at all: they change a rule, generate a class
    resource, or apply only in a condition the three-bucket pipeline cannot
    express. Issue #939 measures the gap and lists what the rest would need.

    A ROW HERE IS OPTIONAL. A node with no row grants nothing, which is what
    every node did before this sheet existed, so an unauthored node is the
    ordinary case rather than an error.

    A NODE MAY HAVE SEVERAL ROWS AND ALL OF THEM APPLY. Issue #953. It could have
    exactly one until then, because the DataTable row name WAS the node name, and
    that shape cannot express the nodes that grant two things: "+1% increased
    Maximum Health and +0.5% increased Armor" is two stats in one sentence, and
    the Masochist's starting node grants three Fervour rates at once. The node is
    now a column of its own and the row name is the node with `#1`, `#2` and so
    on after it, in the order the sheet lists them. Nothing reads the row name;
    it exists because a DataTable needs a unique key.

    A NUMBER SIGN IS THE SEPARATOR BECAUSE NO NODE NAME CAN CONTAIN ONE. Node
    names are a tree name and an identifier from the tree authoring tool joined
    by an underscore, so an underscore would let `A_1` mean either the first row
    of node `A` or the only row of a node really called `A_1`.
    """
    rows = list(book["Passive Effects"].iter_rows(values_only=True))
    headers = _header_index(rows, "Passive Effects")

    # A CAP ON THIS SHEET SINCE 2026-11-24. Issue #1815 refused the column
    # because a passive node's modifier carried no cap, so a cap written here
    # was dropped with no error. The demonic tree rework the owner approved
    # that day needs one: the Ritualist's Wall of Servants reads "You take 1%
    # less damage for each minion you have, up to 20% less", and the owner
    # answered that the per-minion damage reduction is capped at 20%. So the
    # row struct carries `ScaleMaxSteps` and `UCataclysmPassiveTree::
    # AccumulateInto` copies it onto the modifier, which `ScaledValue` caps by,
    # as an enchantment's is. It is read below, by the same rules the
    # Enchantment Effects sheet keeps.

    # STACK SECONDS AND AN OFFSET ARE READ HERE SINCE THE PASSIVE ROW PORT OF
    # 2026-11-24, with the rest of the action columns (`PASSIVE_ACTION_COLUMNS`).
    # Issues #1833 and #1686 refused them, because the game read a row's own
    # stacks and an offset from the Enchantment Effects sheet only; a passive
    # row stating one is now built by that sheet's builder in the game.
    #
    # THE COLUMNS ABOUT AN ITEM ARE STILL REFUSED. A passive row rolls nothing
    # and repeats no enchantment's words.
    for column in ENCHANTMENT_ONLY_COLUMNS:
        if column in headers:
            raise DataError(
                f"the Passive Effects sheet has a {column!r} column, which is "
                f"about an item: its enchantment, its words or the roll that "
                f"picks a value inside a range. A passive row names its node and "
                f"is paid per point. Remove it.")

    # EACH NODE'S WORDS, read only when a row states an action column. See
    # `_passive_action_row`.
    sentences: dict[tuple[str, int], str] | None = None

    out = []
    counts: dict[str, int] = {}
    for index, raw in enumerate(rows[1:], start=2):
        node = clean(_cell(raw, headers, "Node"))
        if not node:
            continue

        if "#" in node:
            raise DataError(
                f"Passive Effects row {index}: the node name {node!r} contains "
                f"a number sign, which this file uses to separate a node from "
                f"the index of its effect. Rename the node.")

        # THE THREE COLUMNS ABOUT THE NODE'S POINTS AND OPTIONS, read first,
        # because both kinds of row below carry them by the same rules.
        option = _passive_option(raw, headers, index, node)
        min_points = _passive_min_points(raw, headers, index, node)
        per_point_from = _per_point_from(raw, headers, index, node, min_points)
        max_points = _max_points(raw, headers, index, node, min_points)

        # A ROW STATING AN ACTION COLUMN, OR SCALED BY A COUNT AN ACTION GRANTS,
        # IS READ BY THE ENCHANTMENT EFFECTS SHEET'S OWN RULES. The passive row
        # port of 2026-11-24. See `PASSIVE_ACTION_COLUMNS` and
        # `_passive_action_row`.
        if _passive_row_is_action_shaped(raw, headers):
            if sentences is None:
                sentences = _passive_node_sentences()
            shared = _passive_action_row(raw, headers, index, node,
                                         sentences.get((node, option), ""))

            # NO RADIUS, because the enchantment rows' builder carries none: it
            # would count nobody. No authored passive row needs both yet.
            reach_named = PASSIVE_REACH_NAMES & {
                shared["Condition"], shared["Condition2"], shared["Scale"]}
            if reach_named or clean(_cell(raw, headers, "Reach Metres")):
                raise DataError(
                    f"Passive Effects row {index}: {node} states an action column "
                    f"and counts the enemies within a radius "
                    f"({', '.join(sorted(reach_named)) or 'Reach Metres'}). A "
                    f"row with an action column is built by the enchantment rows' "
                    f"builder, which carries no radius, so it would count nobody. "
                    f"Write the radius-counting part as a row of its own.")
            # NOR THE SCALE THAT COUNTS ENEMIES WHILE AN OWN STACK IS HELD, which
            # needs a second row sharing its key; no passive row needs it yet.
            if shared["Scale"] == WINDOW_SCALE:
                raise DataError(
                    f"Passive Effects row {index}: {node} scales by "
                    f"{WINDOW_SCALE!r}, which the Passive Effects sheet does not "
                    f"read yet. No passive row needs it; add it with the row that "
                    f"does.")

            counts[node] = counts.get(node, 0) + 1
            out.append({
                "Name": f"{node}#{counts[node]}",
                "Node": node,
                "Stat": shared["Stat"],
                "ValueKind": shared["ValueKind"],
                # PER POINT, as every passive value is: `_effect_row` read it
                # where an enchantment's lowest roll would be, and a passive row
                # has no other end to a range.
                "ValuePerPoint": shared["ValueLow"],
                "RequiredTags": shared["RequiredTags"],
                "Condition": shared["Condition"],
                "ConditionValue": shared["ConditionValue"],
                "Scale": shared["Scale"],
                "ScaleStep": shared["ScaleStep"],
                "Option": option,
                "ReachMetres": -1.0,
                "MinPoints": min_points,
                "ScaleMaxSteps": shared["ScaleMaxSteps"],
                **{column: shared[column] for column in PASSIVE_PORT_FIELDS},
                "PerPointFrom": per_point_from,
                # AND THE CHANCE, LAST. The Death trees' slice C6b3 (2026-11-25).
                "ChancePercent": _chance_percent(raw, headers, index, node, shared["Action"]),
                "MaxPoints": max_points,
            })
            continue

        stat = clean(_cell(raw, headers, "Stat"))
        if not stat:
            raise DataError(f"Passive Effects row {index}: {node} names no stat")

        kind = clean(_cell(raw, headers, "Value Kind")).lower()
        _check_value_kind("Passive Effects", index, node, stat, kind)

        # A STATE THE BONUS ONLY APPLIES IN (issue #959) AND A STATE ITS SIZE
        # GROWS WITH (issue #968), CHECKED BY THE SAME HELPER THE ENCHANTMENT
        # SHEETS USE. Empty is "always" and "a fixed value", which is every
        # row before those issues. `_condition_and_scale` refuses a name the
        # game cannot judge, a value beside a condition that compares nothing,
        # a value outside the condition's range, and a step of nothing or
        # outside the scale's range, with the reasons beside each refusal.
        # This function carried its own copy of those checks until issue
        # #1593; one copy means the wording and the rules cannot drift apart.
        condition, condition_value, scale, scale_step = _condition_and_scale(
            raw, headers, "Passive Effects", index, node)

        # "UP TO 20% LESS" IS A CAP OF 20 WHOLE STEPS, since 2026-11-24 (see
        # above). THE ENCHANTMENT SHEET'S RULES, KEPT THE SAME: empty is no
        # cap; a cap needs a scale to cap; it is a whole number of steps from 1
        # to MAX_SCALE_STEPS; a scale counting a fraction takes none; and a
        # scale that needs one must state one.
        cap_text = clean(_cell(raw, headers, "Scale Max Steps"))
        scale_max_steps = 0
        if cap_text:
            if scale in SCALES_THAT_COUNT_A_FRACTION:
                raise DataError(
                    f"Passive Effects row {index}: {node} scales by {scale!r} "
                    f"and states Scale Max Steps {cap_text!r}. That scale counts "
                    f"a fraction and not whole steps, and is capped at the row's "
                    f"whole value by itself. Leave the column empty.")
            cap = number(cap_text, "Scale Max Steps", index)
            if not scale:
                raise DataError(
                    f"Passive Effects row {index}: {node} has a cap of {cap:g} "
                    f"steps and no scale. A cap limits how many steps a scale "
                    f"counts, so with no scale it caps nothing.")
            if cap != int(cap) or not 1 <= cap <= MAX_SCALE_STEPS:
                raise DataError(
                    f"Passive Effects row {index}: {node} has a cap of {cap:g} "
                    f"steps. A cap is a whole number of steps from 1 to "
                    f"{MAX_SCALE_STEPS}; leave the column empty for none.")
            scale_max_steps = int(cap)
        if scale in SCALES_THAT_NEED_A_CAP and scale_max_steps < 1:
            raise DataError(
                f"Passive Effects row {index}: {node} scales by {scale!r} and "
                f"states no Scale Max Steps. That scale has no cap of its own, "
                f"so the row would be uncapped. State the cap.")
        # AND THE SCALE THAT COUNTS ENEMIES ONLY WHILE AN OWN STACK IS HELD.
        # Ruled by the project owner on 2026-10-17. A passive node holds no own
        # stacks, so its modifier would have no window to read.
        if scale == WINDOW_SCALE:
            raise DataError(
                f"Passive Effects row {index}: {node} scales by {scale!r}, which "
                f"counts enemies only while an enchantment's own stack is held, "
                f"and a passive node has no own stacks. The row would grant "
                f"nothing.")
        # NOR THE POOL'S MAXIMUM BY ITSELF. The War vow options' second slice,
        # 2026-11-25. See `MAXIMUM_CLASS_RESOURCE_SCALE`.
        if scale == MAXIMUM_CLASS_RESOURCE_SCALE and stat == "class_resource":
            raise DataError(
                f"Passive Effects row {index}: {node} scales `class_resource` by "
                f"{scale!r}. That scale reads the maximum the `class_resource` "
                f"line comes to, and the line is read without the readings a row "
                f"must ask for, so the row would read an unknown maximum and grant "
                f"nothing.")

        # HOW FAR "NEAR" IS, FOR THE TWO NAMES THAT COUNT NEARBY ENEMIES.
        # Issue #1597. A condition carries one number and "three or more
        # enemies within four metres" is two of them, so the radius needs a
        # column rather than a share of `Condition Value`. A scale row has the
        # same problem from the other side: `Scale Step` already holds how
        # many enemies one step is worth.
        #
        # A ROW USING EITHER NAME MUST STATE ONE, because a modifier with no
        # reach counts nobody. That is the failure this refuses: the node
        # would import cleanly, grant nothing, and say so nowhere.
        #
        # AND NO OTHER ROW MAY STATE ONE, because nothing would read it. A
        # number in a column the game never looks at is a statement somebody
        # believes is doing something.
        #
        # A RADIUS PER ROW RATHER THAN ONE CONSTANT, which is the decision of
        # 2026-09-12 in `docs/DECISIONS.md`: Path of Exile's developers state
        # that "nearby" is set per effect, and this game's own rows need 3, 4
        # and 8 metres.
        #
        # THE SAME 0 TO 100 METRE BOUND `target_within_metres` USES, and the
        # same judgement behind it: a radius past 100 would cover any room the
        # game has, so it is far likelier to be a number in the wrong column.
        # AND THE CRIPPLED-ENEMY SCALE, ISSUE #1515, which counts within a
        # radius exactly as the two above do and so needs one just as much.
        counts_enemies = {"enemies_in_reach_at_least", "enemies_in_reach",
                          "crippled_enemies_in_reach",
                          # AND THE DEATH TREES' SLICE C6b3'S TWO (2026-11-25).
                          "enemy_of_rarity_in_reach", "no_enemy_in_reach",
                          # AND SLICE C6d1'S OWN MINIONS.
                          "minions_in_reach_at_least",
                          # AND CLOSE PACKED'S OTHER MINIONS NEAR THE MINION
                          # ASKING, the finishing of the Death Cataclysm, slice C
                          # (2026-11-26).
                          "minions_near_the_minion"}
        # AND GRIM WAKE'S CONDITION, whose radius is around the body of a kill
        # rather than around the character, read at the kill from this column.
        # The Death trees' slice C4c2 (2026-11-25).
        reads_a_radius = counts_enemies | {"target_near_your_kill_within_seconds",
                                           # AND DEATH WARD'S, the Death trees'
                                           # slice C6d2: a minion's death within
                                           # the radius of the character.
                                           "minion_death_in_reach_within_seconds"}
        named = reads_a_radius & {condition, scale}
        written_reach = clean(_cell(raw, headers, "Reach Metres"))
        reach_metres = -1.0
        if named and not written_reach:
            raise DataError(
                f"Passive Effects row {index}: {node} names "
                f"{', '.join(sorted(named))}, which counts the enemies within a "
                f"radius, and states no 'Reach Metres'. A row with no radius "
                f"counts nobody, so the node would grant nothing.")
        if written_reach and not named:
            raise DataError(
                f"Passive Effects row {index}: {node} states a 'Reach Metres' "
                f"of {written_reach!r} and names neither "
                f"{' nor '.join(sorted(counts_enemies))}. Nothing would read "
                f"it. Leave the column empty.")
        if named:
            reach_metres = number(_cell(raw, headers, "Reach Metres"),
                                  "Reach Metres", index)
            if not 0.0 < reach_metres <= 100.0:
                raise DataError(
                    f"Passive Effects row {index}: {node} has a reach of "
                    f"{reach_metres}, and a radius takes metres above 0 and up "
                    f"to 100. A reach of nothing counts nobody.")

        # THE OPTION AND THE MIN POINTS WERE READ AT THE TOP OF THE LOOP, by
        # `_passive_option` and `_passive_min_points`, because the action rows
        # above carry them too.

        counts[node] = counts.get(node, 0) + 1

        out.append({
            "Name": f"{node}#{counts[node]}",
            "Node": node,
            "Stat": stat,
            "ValueKind": kind,
            "ValuePerPoint": number(_cell(raw, headers, "Value Per Point"),
                                    "Value Per Point", index),
            "RequiredTags": clean(_cell(raw, headers, "Required Tags")),
            "Condition": condition,
            "ConditionValue": condition_value,
            "Scale": scale,
            "ScaleStep": scale_step,
            "Option": option,
            "ReachMetres": reach_metres,
            "MinPoints": min_points,
            # AFTER MIN POINTS, so every column before it keeps its place. 0 is
            # no cap. Since 2026-11-24; see above.
            "ScaleMaxSteps": scale_max_steps,
            # AND THE PASSIVE ROW PORT'S COLUMNS, LAST, empty on a row stating
            # none of them, which is every row read here. Since 2026-11-24.
            **dict(PASSIVE_PORT_DEFAULTS),
            "PerPointFrom": per_point_from,
            # AND THE CHANCE, which a stat row may not state: refused here.
            "ChancePercent": _chance_percent(raw, headers, index, node, ""),
            # AND THE UPPER LIMIT, since the Saboteur's rows of 2026-11-25.
            "MaxPoints": max_points,
        })

    # THE SAME NODE AND THE SAME STAT TWICE IS A MISTAKE RATHER THAN A DOUBLE
    # HELPING, and it is one a person editing a spreadsheet makes by copying a
    # row and forgetting to change the stat. Two rows granting the same node two
    # DIFFERENT stats is the whole point of the shape and is allowed.
    #
    # UNLESS THE TWO ARE CONDITIONED DIFFERENTLY, which is a real shape: a node
    # could give one amount always and more of it below a threshold. So the
    # condition is part of what makes a row distinct.
    #
    # AND SO IS THE SCALE, since issue #968, for the same reason: a node could
    # give a fixed amount and a further amount that grows with a state.
    #
    # AND SO IS THE CAPSTONE OPTION, since issue #1029, and that one is not a
    # refinement but a necessity. A capstone's three options are three separate
    # things a player may pick, and two of them may reasonably move the same
    # stat by different amounts. Without the option in this key the second would
    # read as a duplicated row and the sheet would refuse to be written at all.
    #
    # AND SO IS THE POINT THRESHOLD, since issue #1755: a node could grant a
    # stat per point and the same stat again, whole, from "At 4 points".
    #
    # AND SO ARE THE ACTION COLUMNS AND A SECOND CONDITION, since the passive
    # row port of 2026-11-24, as the Enchantment Effects sheet's key has them:
    # one node may restore health and generate Fervour on the same event.
    #
    # AND SO ARE THE REQUIRED TAGS, since the Death trees' slice C6a
    # (2026-11-25): a row requires every tag it names, so "Poison and Bleed you
    # apply" is one row for each, the same stat under two tags (Lasting Wounds).
    # The Saboteur's rows of the same day need it too: Bag of Tricks lowers what
    # traps reserve and what gadgets reserve, one stat on `Type.Trap` and on
    # `Type.Gadget`, two things a player gets.
    pairs: dict[tuple, int] = {}
    for row in out:
        key = (row["Node"], row["Stat"], row["Condition"],
               row["ConditionValue"], row["Scale"], row["ScaleStep"],
               row["Option"], row["MinPoints"], row["Action"],
               row["ActionEvent"], row["FractionOf"], row["Condition2"],
               row["ConditionValue2"], row["RequiredTags"], row["MaxPoints"])
        pairs[key] = pairs.get(key, 0) + 1
    twice = sorted(key for key, count in pairs.items() if count > 1)
    if twice:
        listed = ", ".join(f"{key[0]} granting {key[1]}" for key in twice)
        raise DataError(
            f"the Passive Effects sheet grants the same stat twice on one "
            f"node, under the same condition, scale and capstone option: "
            f"{listed}. Two rows for one node are for two different stats, or "
            f"for the same stat under different conditions, scales or options; "
            f"anything else is a duplicated row.")

    # ONE ROW PER NODE, OPTION, ACTION AND EVENT. The game keys what an action
    # row keeps -- its trigger cooldown, a next-use charge, a count of every
    # Nth event, a kept stack -- by the node, the option, the action and the
    # event (`UCataclysmPassiveTree::EnchantmentRowsFor`), as an enchantment's
    # are keyed by its name, action and event. Two rows sharing all four would
    # share one cooldown and one count. The passive row port of 2026-11-24.
    #
    # AN ACTION ROW FROM A POINT THRESHOLD HAS A KEY OF ITS OWN since the Death
    # trees' slice C6a (2026-11-25): `EnchantmentRowsFor` names it apart by its
    # `MinPoints`, so "1 second for every 4 points" is a row at 4 and a row at 8.
    #
    # AND A SECOND ROW ON THE SAME ACTION AND EVENT UNDER A DIFFERENT CONDITION
    # HAS ONE since the Death trees' slice C6b3 (2026-11-25): `EnchantmentRowsFor`
    # names it apart as `_Row2`, so Opening Found's "1 Fervour per point, or 2 per
    # point if the attacker is Elite or stronger" is 1 always and 1 more under
    # `target_rarity_at_least` 1. Two rows under the SAME conditions are still
    # refused: nothing would tell them apart but their order.
    keyed: dict[tuple, set] = {}
    for row in out:
        if row["Action"]:
            key = (row["Node"], row["Option"], row["Action"], row["ActionEvent"],
                   row["MinPoints"])
            conditions = (row["Condition"], row["ConditionValue"],
                          row["Condition2"], row["ConditionValue2"])
            seen = keyed.setdefault(key, set())
            if conditions in seen:
                keyed[key] = None
            elif seen is not None:
                seen.add(conditions)
    shared_keys = sorted(key for key, seen in keyed.items() if seen is None)
    if shared_keys:
        listed = ", ".join(f"{node} option {option} {action} on {event or '(none)'}"
                           + (f" from {points} points" if points else "")
                           for node, option, action, event, points in shared_keys)
        raise DataError(
            f"the Passive Effects sheet gives one node two rows of the same "
            f"action on the same event under the same conditions: {listed}. The "
            f"game keeps one cooldown and one count for each such row, and nothing "
            f"would tell the two apart. Write the two as one row.")

    if not out:
        raise DataError("the Passive Effects sheet is empty")
    return out


#: The pools an effect row may move, and nothing else is a pool.
#:
#: EACH NAMES TWO ATTRIBUTES ON THE GAME SIDE -- what is held and the most
#: that can be held -- and `UCataclysmAbilitySystemComponent::PoolAttributesFor`
#: is what turns a name here into that pair. A name that function does not know
#: would be a row the game reads, finds no pair for, and does nothing with but
#: log a warning. `tools/tests/test_pool_action_names_match_the_engine.py`
#: holds the two lists equal.
POOL_ACTIONS = (
    "health",
    "mana",
    "energy_shield",
    "class_resource",
)

#: The actions that grant a charge the next use spends, rather than moving a
#: pool. Issue #1833, phase 2: "When you dodge an attack your next skill
#: deals 30%-60% increased damage". The row's value is what one charge is
#: worth as increased damage, Scale Max Steps is how many it holds, and the
#: Action Event grants one.
#:
#: "SKILL" AGAINST "ATTACK", ruled 2026-09-24: any use that delivers damage
#: spends a skill charge, and a skill tagged `Type.Spell` does not spend an
#: attack charge. `UCataclysmAbilitySystemComponent::NextSkillDamageAction`
#: and `NextAttackDamageAction` hold the same two names.
NEXT_USE_ACTIONS = (
    "next_skill_damage",
    "next_attack_damage",
    # "Every 30 seconds your next skill is cast at 300%-500% effectiveness".
    # A skill charge whose value multiplies the spending use's damage: 300
    # is three times. Ruled 2026-09-24: this project reads "effectiveness"
    # as a skill's own coefficient.
    "next_skill_effectiveness",
    # "Each spell cast reduces your next spell cooldown by 0.5-1.5 seconds".
    # Issue #1833, the cooldown reduction action, ruled 2026-09-25. Its value is
    # SECONDS taken off the next spell's cooldown when it is applied, not
    # damage; cap 1, because "your next spell" is one.
    "next_spell_cooldown_reduced",
)

#: The actions that PLACE A STACK ON THE OTHER CHARACTER of their event,
#: each with the events that name that character. Issue #1833, phase 2:
#: "Strike skills reduce enemy armor by 3%-6% per hit for 5 seconds, up to 6
#: stacks" and "Each melee hit you take reduces the attacker's damage by 3%-5%
#: for 3 seconds, stacking up to 5 times". The value is what one stack takes
#: away, Stack Seconds the window (a grant refreshes it, ruled 2026-09-24) and
#: Scale Max Steps the cap, empty for none ("stacking indefinitely").
#: `UCataclysmAbilitySystemComponent::EnemyArmorRemovedAction` and
#: `AttackerDamageRemovedAction` hold the same two names.
PLACED_ACTIONS = {
    "enemy_armor_removed": ("hit_dealt", "deployable_hit"),
    "attacker_damage_removed": ("melee_hit_taken",),
    # THE SAME ARMOUR STACK PLACED ON EVERY ENEMY NEAR THE WEARER, within the
    # nearby radius of five metres, rather than on the event's own character.
    # The War vow options' second slice, V2b, 2026-11-25: Warlord, "your melee
    # attacks reduce the armor of all enemies within 5 meters by 15% for 3
    # seconds. This effect refreshes on each hit", ruled "all enemies within 5
    # m". The window and the cap are read as for the one above.
    # `UCataclysmAbilitySystemComponent::EnemyArmorRemovedNearbyAction` holds
    # the same name.
    "enemy_armor_removed_nearby": ("hit_dealt",),
    # A STACK RAISING THE RETALIATION THE ATTACKER TAKES, placed on whoever
    # landed a hit on the wearer. The War vow options' third slice, V3a,
    # 2026-11-25: Thornwall, "Enemies that hit you are inflicted with a stack of
    # Exposed. Each stack of Exposed increases the retaliation damage they take
    # from you by 5%, stacking up to 10 times", ruled "any hit: pass the attacker
    # on hit taken", a landed hit and not a tick, 10 seconds refreshed by each
    # hit. `hit_taken` names the attacker since that slice.
    # `UCataclysmAbilitySystemComponent::AttackerRetaliationTakenAction` holds
    # the same name.
    "attacker_retaliation_taken": ("hit_taken",),
}

#: The actions that COUNT A KIND OF EVENT AND ACT ON EVERY Nth, each naming
#: what it counts, so the row states no Action Event. Issue #1833, phase 2,
#: ruled 2026-09-24. Every Nth is N. The value is the Nth hit taken's share
#: taken on top, or the share of the mana held the Nth spell adds to its cost;
#: an attack deals no damage, which is written 100.
#: `UCataclysmAbilitySystemComponent::NthHitTakenDamageAction` and its two
#: siblings hold the same three names.
NTH_ACTIONS = (
    "nth_hit_taken_damage",
    "nth_spell_mana_cost",
    "nth_attack_no_damage",
)

#: The largest N a row may state.
MAX_EVERY_NTH = 100

#: The actions that CLEAR SKILL COOLDOWNS, each naming which. Issue #1833, the
#: cooldown reset action, ruled 2026-09-25. The value is the chance in percent,
#: above 0 and up to 100, where 100 is always: "Blocking an attack has a
#: 20%-40% chance to reset your heavy attack cooldown" is 20 to 40, "Your
#: special ability cooldown is reset when you kill an enemy" is 100. `others`
#: is every slot but the one the event's skill is in, and `event_skill` that
#: one; both read the event's `Slot.*` tag.
#: `UCataclysmAbilitySystemComponent::CooldownResetAllAction` and its five
#: siblings hold the same six names.
COOLDOWN_RESET_ACTIONS = (
    "cooldown_reset_all",
    "cooldown_reset_others",
    "cooldown_reset_heavy",
    "cooldown_reset_special",
    "cooldown_reset_movement",
    "cooldown_reset_event_skill",
)

#: The actions that TAKE SECONDS OFF RUNNING SKILL COOLDOWNS, each naming
#: which. Issue #1833, the cooldown reduction action, ruled 2026-09-25. The value
#: is seconds, above 0 and up to `MAX_COOLDOWN_REDUCE_SECONDS`. A cooldown with
#: no more than that left ends, and nothing carries over. Only the three targets
#: a row asks for. `UCataclysmAbilitySystemComponent::CooldownReduceAllAction`
#: and its two siblings hold the same three names.
COOLDOWN_REDUCE_ACTIONS = (
    "cooldown_reduce_all",
    "cooldown_reduce_heavy",
    # EVERY SKILL BUT THE ONE THAT RAISED THE EVENT, read off its `Slot.*` tag as
    # `cooldown_reset_others` reads it: Turn of the Wheel, "Every 10 Fervour you
    # spend takes 1 second off the cooldowns of your skills that did not spend
    # it". The Death trees' slice C3b (2026-11-25).
    "cooldown_reduce_others",
    # THE MOVEMENT SKILL'S: the Shadow's Find the Rhythm and Sidestep, "takes 1
    # second ... off your movement skill's remaining cooldown". The Death trees'
    # slice C6a (2026-11-25).
    "cooldown_reduce_movement",
)

#: The most seconds a reduction may take off, the same sanity bound the clocks use.
MAX_COOLDOWN_REDUCE_SECONDS = 60.0

#: The action that APPLIES A RANDOM DAMAGE OVER TIME TO THE OTHER CHARACTER of
#: its event: one of Bleed, Poison, Disease, Necrosis and Burn, equally likely,
#: at the ailment's normal magnitude. Issue #1833 group D, ruled 2026-09-30:
#: "Critical strikes apply a random DoT to the target". The value is the chance
#: in percent, above 0 and up to 100, where 100 is always.
#: `UCataclysmAbilitySystemComponent::ApplyRandomDotAction` holds the same name.
RANDOM_DOT_ACTION = "apply_random_dot"

#: The events a random damage over time may hang on: the ones naming the other
#: character AND carrying what reached its health, because the owner's rule of
#: 2026-09-02 (#917) lets an ailment that does not come from the skill's own row
#: land only from a blow that took a tenth of the target's maximum health.
#: `hit_dealt` names who was struck and carries no amount, so it is not here.
RANDOM_DOT_EVENTS = (
    "critical_strike",
    "retaliation_dealt",
)

#: The events A HIT DEALT OR TAKEN fires, on which an action row that makes
#: something happen waits `DEFAULT_TRIGGER_COOLDOWN` after it fires unless its
#: Trigger Cooldown says otherwise. Issue #1833 group D, ruled 2026-09-30 under
#: the owner's delegation, building the judgement of 2026-09-11: "0.25 s for a
#: trigger fired by a hit dealt or taken (critical strike, hit, block, evade);
#: none for kills, deaths, timers and resource events". Retaliation is paid
#: because a hit was taken, so it is here too.
HIT_FIRED_EVENTS = (
    "hit_dealt",
    "critical_strike",
    "hit_taken",
    "melee_hit_taken",
    "block",
    "dodge",
    "deployable_hit",
    "minion_hit",
    "retaliation_dealt",
    # A BLOW BREAKING THE ENERGY SHIELD. Issue #1833 group D part 3, ruled
    # 2026-10-01: a hit causes it, so it takes the default.
    "energy_shield_broken",
    # NOT `energy_shield_recharged`, issue #1833 group E part 2: no hit causes a
    # recharge, which waits until no damage has been taken for a while.
    # THE WEARER'S FIRST BLOW TO GET THROUGH TO AN ENEMY. Issue #1833 group E
    # part 1, ruled 2026-10-01: a hit dealt, so it takes the default.
    "first_hit_dealt",
)

#: The seconds a trigger waits when its row states none. See `HIT_FIRED_EVENTS`.
DEFAULT_TRIGGER_COOLDOWN = 0.25

#: The longest trigger cooldown a row may state: an hour, so a set bonus's
#: "(10 minute cd)" fits. The clocks' 60 s bound was the reason none could.
MAX_TRIGGER_COOLDOWN = 3600.0


#: The action that LOWERS HEALTH TO A SHARE OF ITS MAXIMUM and never raises it.
#: Issue #1833 group D part 2, ruled 2026-09-30: "You start every dungeon floor
#: at 30%-50% of your maximum HP". The value is the share in percent, above 0 and
#: below 100. `UCataclysmAbilitySystemComponent::HealthCappedAtAction` holds the
#: same name.
HEALTH_CAP_ACTION = "health_capped_at"

#: The event that needs a threshold, and so the one event that reads Event
#: Value: health falling from at or above that share of its maximum to below it.
#: Issue #1833 group D part 2, ruled 2026-09-30.
THRESHOLD_EVENT = "health_falls_below"

#: The actions on the characters NEAR THE WEARER, within five metres, the
#: judgement of 2026-09-11 for "nearby" with no number. Issue #1833 group D part
#: 3, ruled 2026-10-01. `smite_nearby` is a hit of the value's share of weapon
#: damage on every enemy nearby, "Smite ... a nova at 100% of weapon damage";
#: `heal_nearby_enemies` heals each enemy nearby by the value's share of its own
#: maximum health, and never a character on the wearer's side.
#: `UCataclysmAbilitySystemComponent::SmiteNearbyAction` and
#: `HealNearbyEnemiesAction` hold the same names.
NEARBY_ACTIONS = (
    "smite_nearby",
    "heal_nearby_enemies",
    # A BLOW OF A SHARE OF THE WEARER'S ARMOUR on every enemy nearby, meeting
    # each one's mitigation as a blow does. Issue #1833 group E part 3, ruled
    # 2026-10-02: "Blocking attacks deals 200%-400% of your armor as damage to
    # nearby enemies". `SmiteNearbyByArmourAction` holds the same name.
    "smite_nearby_by_armor",
    # A BLOW OF A SHARE OF WHAT THE EVENT'S HIT TOOK FROM HEALTH on every enemy
    # within the row's Event Value in metres of the wearer, the struck one
    # included, meeting each one's mitigation as a blow does. The War vow
    # options' second slice, V2b, 2026-11-25: Pure Carnage, "critical strikes
    # release a nova of force that deals 30% of the hit's damage to all enemies
    # within melee range", ruled "a share of what the hit took from health", and
    # Ragnarok's shockwaves, 50% within 6 metres. Its radius is its own, so it is
    # not the five metres above; see `SMITE_NEARBY_BY_HIT_ACTION`.
    # `SmiteNearbyByHitAction` holds the same name.
    "smite_nearby_by_hit",
)

#: The nova sized by a hit, its events and its radius. The War vow options'
#: second slice, V2b, 2026-11-25. ONLY THE EVENTS THAT CARRY WHAT A HIT TOOK
#: FROM HEALTH, which `ACataclysmPlayerCharacter` raises with the hit notice's
#: `DealtToHealth`. THE RADIUS IS THE ROW'S EVENT VALUE IN METRES, required, as
#: the gadget's shot at a trap's enemy reads its own there: Pure Carnage's "melee
#: range" is 4 (the design document's melee range) and Ragnarok's 6.
SMITE_NEARBY_BY_HIT_ACTION = "smite_nearby_by_hit"
SMITE_NEARBY_BY_HIT_EVENTS = ("hit_dealt", "first_hit_dealt", "critical_strike")
MAX_SMITE_NEARBY_BY_HIT_METRES = 20.0

#: The largest share a nearby action may state: a smite of ten times weapon
#: damage, or a heal of a whole maximum. A sanity bound, as the clocks' is.
MAX_NEARBY_PERCENT = {
    "smite_nearby": 1000.0,
    "heal_nearby_enemies": 100.0,
    "smite_nearby_by_armor": 1000.0,
    "smite_nearby_by_hit": 1000.0,
}

#: The action that PAYS A SHARE OF WHAT A BLOCK REMOVED BACK TO THE ATTACKER, as
#: retaliation pays. Issue #1833 group E part 3, ruled 2026-10-02: "Reflect
#: 20%-100% of damage blocked back at attackers". The value is the share, above
#: 0 and up to 100, and only `block` carries what it needs.
#: `UCataclysmAbilitySystemComponent::ReflectBlockedAction` holds the same name.
REFLECT_BLOCKED_ACTION = "reflect_blocked"

#: The events a reflect may hang on: the ones carrying the attacker and what
#: the block removed. Issue #1833 group E part 3.
REFLECT_BLOCKED_EVENTS = (
    "block",
)

#: The action that BLASTS EVERY ENEMY NEAR AN ENEMY THAT DIED for a share of the
#: dead enemy's maximum health, and then puts the wearer's ailments that were on
#: the body on them. Ruled 2026-10-07: "Plague Doctor (10-Piece Bonus): When an
#: enemy dies while affected by a DoT from you, it explodes and applies all of
#: your DoTs to all nearby enemies". The value is the share, above 0 and up to
#: 100. `UCataclysmAbilitySystemComponent::BlastFromTheDyingAction` holds the
#: same name.
BLAST_FROM_THE_DYING_ACTION = "blast_from_the_dying"

#: The events that blast may hang on: the ones raised on whoever afflicted an
#: enemy that died, carrying the dead enemy, its maximum health and, as tags,
#: that character's ailments on the body. NOT `kill`: it carries the first two,
#: but its tags are the killing skill's, so the copies would never be made.
BLAST_FROM_THE_DYING_EVENTS = (
    "afflicted_death",
)

#: The action that STUNS EVERY ENEMY NEAR AN ENEMY THAT DIED carrying one of the
#: wearer's damage over time ailments. Ruled 2026-10-08: "Chronomancer's
#: Time-Lock (6-Piece Bonus): When an enemy dies while affected by one of your
#: debuffs, there is a 25% chance for a 'Time-Lock' to occur. This freezes all
#: nearby enemies for 2 seconds". The owner, 2026-10-08: "Yes it acts like a
#: stun". TWO FIGURES ON ONE ROW: the value is the chance, above 0 and up to
#: 100, rolled once for the death; Stack Seconds is how long the stun lasts,
#: above 0 and up to `MAX_STATUS_SECONDS`. Both are required.
#: `UCataclysmAbilitySystemComponent::StunNearTheDyingAction` holds the same
#: name.
STUN_NEAR_THE_DYING_ACTION = "stun_near_the_dying"

#: The events that stun may hang on: the one raised on whoever afflicted an
#: enemy that died, carrying the dead enemy as the centre. NOT `kill`: the
#: sentence asks whose debuff the enemy carried and not who landed the last
#: blow, and `kill` is raised on the killer alone.
STUN_NEAR_THE_DYING_EVENTS = (
    "afflicted_death",
)

#: The actions that DEAL THE REMAINING DAMAGE of the wearer's own damage over
#: time effects, as one instance each. Issue #1833 group D part 4, ruled
#: 2026-10-01. The value is a percentage of that remaining damage, above 0 and up
#: to 100. `dot_remaining_nearby` reaches every enemy within five metres and ends
#: each effect: "When you die, all active DoTs on nearby enemies instantly deal
#: their remaining damage". `dot_remaining_target` reaches the event's other
#: character and ends nothing: "Necrosis effects deal 20%-40% of their remaining
#: damage instantly when you land a critical strike".
#: `UCataclysmAbilitySystemComponent::RemainingDamageNearbyAction` and
#: `RemainingDamageTargetAction` hold the same names.
REMAINING_DAMAGE_ACTIONS = (
    "dot_remaining_nearby",
    "dot_remaining_target",
)

#: The events `dot_remaining_target` may hang on: the ones naming the character
#: the wearer struck. Issue #1833 group D part 4.
REMAINING_DAMAGE_TARGET_EVENTS = (
    "hit_dealt",
    "critical_strike",
    "retaliation_dealt",
)

#: The ailments an Ailment cell may name: the five damage over time ailments,
#: as `UCataclysmAilments::KindNamed` reads them. Issue #1833 group D part 4,
#: ruled 2026-10-01. Required on `dot_remaining_target`, allowed on
#: `dot_remaining_nearby`, and refused on every other row.
AILMENTS = (
    "Bleed",
    "Poison",
    "Disease",
    "Necrosis",
    "Burn",
)

#: The actions that APPLY A STATUS to the other character of their event, named
#: in the row's Ailment cell. Issue #1833 group E part 1, ruled 2026-10-01.
#: `apply_status`'s value is the chance, above 0 and up to 100, at the status's
#: own duration; `apply_status_seconds`'s is the seconds, up to
#: `MAX_STATUS_SECONDS`, and it always applies.
#: `UCataclysmAbilitySystemComponent::ApplyStatusAction` and
#: `ApplyStatusSecondsAction` hold the same names.
APPLY_STATUS_ACTIONS = (
    "apply_status",
    "apply_status_seconds",
)

#: AND A THIRD ON THE OTHER CHARACTER SINCE TRAPS LAYER 3, ruled 2026-10-21: its
#: value is the SIZE of the one application, a whole number of times the
#: ailment's ordinary size at each end, and it always applies. For "Traps apply
#: 1-2 bleed stacks to all enemies caught in their blast", on `deployable_hit`
#: (ruling 8 of 2026-10-07: one bleed at 1 to 2 times its size). It names one of
#: the five damage over time `AILMENTS`, whose size is what a tick deals, on the
#: events of `APPLY_STATUS_EVENTS`. Kept out of `APPLY_STATUS_ACTIONS`, whose two
#: names a merged test pins. `UCataclysmAbilitySystemComponent::
#: ApplyStatusSizedAction` holds the same name.
APPLY_STATUS_SIZED_ACTION = "apply_status_sized"

#: The events a status action may hang on: the ones naming the character the
#: wearer struck, or the attacker it paid retaliation to. Issue #1833 group E
#: part 1.
APPLY_STATUS_EVENTS = (
    "hit_dealt",
    "first_hit_dealt",
    "critical_strike",
    "retaliation_dealt",
    "deployable_hit",
    "minion_hit",
)

#: The statuses `apply_status` may name: every ailment
#: `UCataclysmAilments::Apply` applies, a stagger, and one debuff from
#: `UCataclysmAilments::RandomDebuffPool`. Stun is not here, because `Apply`
#: leaves it to a landed blow's pool; the random debuff reaches it.
#: `UCataclysmAbilitySystemComponent::StaggerStatus` and `RandomDebuffStatus`
#: hold the last two names. Issue #1833 group E part 1.
APPLY_STATUSES = (
    "Bleed",
    "Poison",
    "Disease",
    "Void Splinter",
    "Necrosis",
    "Burn",
    "Madness",
    "Cripple",
    "Weaken",
    "Shred",
    "Stagger",
    "Random Debuff",
)

#: One more status, which `apply_status` ALONE may name: EACH OF THE FIVE DAMAGE
#: OVER TIME AILMENTS of `AILMENTS`, applied in turn. The owner, 2026-10-08, for
#: "Your first hit against each enemy applies all your active DoTs instantly":
#: "Go with the first". The row's chance is rolled once for all five. NOT IN
#: `APPLY_STATUSES`, AND ON PURPOSE: `apply_status_to_self` reads that list too,
#: and this name is not for the wearer; nor may `apply_status_seconds` name it,
#: because five ailments have five durations and a row states one.
#: `UCataclysmAbilitySystemComponent::AllDotsStatus` holds the same name.
ALL_DOTS_STATUS = "All DoTs"

#: And one more, which `apply_status` ALONE may name: A SILENCE on the other
#: character of the event, for `UCataclysmSilence::SilenceSeconds`, one second.
#: Ruled 2026-10-17, for "Null Emperor (2-Piece Bonus): Your attacks have a 25%
#: chance to silence an enemy for 1 second". NOT IN `APPLY_STATUSES`, because
#: `apply_status_to_self` reads that list too and nothing a wearer does reads a
#: silence; nor may `apply_status_seconds` name it, because the sentence states
#: the second and the row states only its chance.
#: `UCataclysmAbilitySystemComponent::SilenceStatus` holds the same name.
SILENCE_STATUS = "Silence"

#: One more status, which `apply_status_to_self` ALONE may name: ONE OF THE
#: WEARER'S PER-TYPE RESISTANCES, DRAWN AND HELD AT NOUGHT until the row fires
#: again. Ruled 2026-10-12 under the owner's delegation, for "Every 5 seconds,
#: one of your resistances is reduced to 0 until this triggers again". NOT IN
#: `APPLY_STATUSES`: that list is read for the other character of an event
#: too, and an enemy holds no per-type resistance to draw from. Nor may the
#: seconds or the sized form name it: the hold lasts until the next firing and
#: has no size. `UCataclysmAbilitySystemComponent::RandomResistanceAtNoughtStatus`
#: holds the same name.
RANDOM_RESISTANCE_AT_NOUGHT_STATUS = "Random Resistance At Nought"

#: And one more, for a wearer only: A RANDOM BUFF OR A RANDOM DEBUFF, at even
#: odds. Ruled 2026-10-12 under the owner's delegation, for "Shard of Anarchy
#: (2-Piece Bonus): Every 30 seconds, a random buff or debuff is applied to
#: you". A drawn debuff is the draw "Random Debuff" makes; a drawn buff is one
#: entry of a table in the engine, held for 30 seconds, which is the engine's
#: figure and not this row's value. A buff is a set of lines on a character's
#: stat line, and an enemy has no stat line, so this is not for the other
#: character of an event.
#: `UCataclysmAbilitySystemComponent::RandomBuffOrDebuffStatus` holds the same
#: name.
RANDOM_BUFF_OR_DEBUFF_STATUS = "Random Buff or Debuff"

#: Every status `apply_status_to_self` alone may name, beside `APPLY_STATUSES`.
APPLY_STATUSES_TO_SELF_ONLY = (
    RANDOM_RESISTANCE_AT_NOUGHT_STATUS,
    RANDOM_BUFF_OR_DEBUFF_STATUS,
)

#: The actions that lay a status ON THE WEARER. Ruled 2026-10-06, for five
#: drawbacks: "Taking a hit has a 15%-25% chance to trigger a random negative
#: status effect on you" and the four beside it. The first two are the two
#: above with the wearer as the character the status is laid on; the third
#: applies it once at the value's multiple of its ordinary size. NO AILMENT IN
#: THE GAME STACKS, so "1-4 stacks are applied to you" is read as size.
#: `UCataclysmAbilitySystemComponent::ApplyStatusToSelfAction`,
#: `ApplyStatusToSelfSecondsAction` and `ApplyStatusToSelfSizedAction` hold the
#: same names.
#:
#: THE RULE OF A TENTH IS NOT ASKED OF THEM. The owner's rule of 2026-09-02
#: (#917) is about what a blow may lay on its target; these sentences state
#: their status without condition, and most have no blow.
APPLY_STATUS_TO_SELF_ACTIONS = (
    "apply_status_to_self",
    "apply_status_to_self_seconds",
    "apply_status_to_self_sized",
)

#: Every status action, on the other character or on the wearer.
ALL_APPLY_STATUS_ACTIONS = (APPLY_STATUS_ACTIONS + APPLY_STATUS_TO_SELF_ACTIONS
                            + (APPLY_STATUS_SIZED_ACTION,))

#: The events a status laid on the wearer may hang on. It needs no other
#: character, so it may wait on a hit taken, a timer, or a skill ending.
APPLY_STATUS_TO_SELF_EVENTS = (
    "hit_taken",
    "critical_strike",
    "every_seconds",
    "skill_end",
    "dot_applied",
)

#: What `apply_status_to_self_seconds` may name: the two above and a stun,
#: which is laid for the row's seconds and keeps its immunity window.
#: `UCataclysmAbilitySystemComponent::StunStatus` holds the name.
APPLY_STATUSES_TO_SELF_FOR_SECONDS = (
    "Stagger",
    "Cripple",
    "Stun",
)

#: What `apply_status_to_self_sized` names, and it names nothing else: the
#: ailment the wearer has just applied to another character. Only on
#: `dot_applied`, which is the event that says one was.
#: `UCataclysmAbilitySystemComponent::AppliedDotStatus` holds the name.
APPLIED_DOT_STATUS = "Applied DoT"

#: The largest multiple of its ordinary size `apply_status_to_self_sized` may
#: lay a status at.
MAX_STATUS_SIZE = 10

#: The statuses `apply_status_seconds` may name: the two whose duration a row
#: may state in place of their own. Cripple keeps its row's strength for the
#: stated seconds. Issue #1833 group E part 1.
#:
#: AND STUN, ON ANOTHER CHARACTER, SINCE 2026-11-25: the coordinating session's
#: ruling for Riposte's "stuns the target for 1 second" and the Saboteur's
#: Concealed (R20), made general. A DESIGNED STUN for the row's seconds
#: (`UCataclysmAbilitySystemComponent::ApplyStatusOf`). NOT IN `APPLY_STATUSES`,
#: for the reason that list gives: a stun has no length of its own to fall back
#: on, so only a row that states its seconds may lay one.
APPLY_STATUSES_FOR_SECONDS = (
    "Stagger",
    "Cripple",
    "Stun",
)

#: The longest a stated status may last. A sanity bound, as the clocks' is.
MAX_STATUS_SECONDS = 10.0

#: The action that OPENS THE WEARER'S NO-DAMAGE WINDOW for its value in seconds,
#: above 0 and up to `MAX_DAMAGE_IMMUNITY_SECONDS`. Issue #1833 group E part 2,
#: ruled 2026-10-02: "When you block an attack, you become immune to all damage
#: for 3 seconds. (10s cd)". `UCataclysmAbilitySystemComponent::DamageImmunityAction`
#: holds the same name.
DAMAGE_IMMUNITY_ACTION = "damage_immunity"

#: The longest no-damage window a row may open. A sanity bound.
MAX_DAMAGE_IMMUNITY_SECONDS = 10.0

#: The action that MOVES ITS WEARER TO A RANDOM REACHABLE POINT NEARBY, with its
#: value as the furthest it may be moved IN METRES, above 0 and up to
#: `MAX_TELEPORT_RANDOMLY_METRES`. Ruled 2026-10-12 by the coordinating session
#: under the owner's delegation: "Teleport to a random location within 4 meters
#: when hit". `UCataclysmAbilitySystemComponent::TeleportRandomlyAction` holds
#: the same name.
#:
#: THE METRES ARE THE ROW'S VALUE, as a no-damage window's seconds are. The
#: sentence states one number and the action needs one, so the value column
#: carries it and no other column is read for it.
TELEPORT_RANDOMLY_ACTION = "teleport_randomly"

#: The furthest a row may move its wearer, in metres. A sanity bound.
MAX_TELEPORT_RANDOMLY_METRES = 20.0

#: The events it may hang on: `hit_taken` and no other. The engine acts for a
#: landed hit and not for a tick of damage over time, and `hit_taken` is the
#: one event for which the engine holds whether the blow was a tick.
TELEPORT_RANDOMLY_EVENTS = ("hit_taken",)

#: The action that FIRES EVERY GADGET ITS WEARER COMMANDS, ONCE, AT ONCE: each
#: skill gadget casts its skill once at the target its own clock would choose,
#: at the whole of its damage, ignoring its clock and leaving it as it was.
#: Ruled 2026-10-19 by the coordinating session (C1-C4), for "When you die, all
#: your active traps and gadgets immediately trigger simultaneously": the
#: sentence's gadget half; its trap half joins when traps are built. SINCE
#: TRAPS LAYER 5 (2026-10-22) THE TRAP HALF IS BUILT, as
#: `trigger_traps_nearby` on `player_death`.
#: `UCataclysmAbilitySystemComponent::GadgetsCastNowAction` holds the same name.
#:
#: ITS VALUE IS 100 AND NOTHING ELSE, as a cleanse's is. The game does not read
#: it: every cast is at the whole of its damage (C2), so the row has no size.
#: NO TRIGGER COOLDOWN: its one event is raised once per death.
GADGETS_CAST_NOW_ACTION = "gadgets_cast_now"

#: The events it may hang on: the player's death and no other. Ruled
#: 2026-10-19 (C1).
GADGETS_CAST_NOW_EVENTS = ("player_death",)

#: The action that CLEANSES THE WEARER: it removes every damage over time, stun,
#: debuff and fear on the wearer that the wearer did not put there itself, which
#: is what `UCataclysmDebuffs::Cleanse` removes. Issue #1833, ruled 2026-09-26
#: and written as a row on 2026-10-05: "You are cleansed every 5 seconds".
#: `UCataclysmAbilitySystemComponent::CleanseAction` holds the same name.
#:
#: ON THE TIMED EVENT AND NO OTHER. `StepTimedGrants` is the one place the game
#: reads the flag; the event loop in `ActOnEvent` has no case for it, so a
#: cleanse on a kill or a block would fall through to the pool path and move
#: nothing. Refused here rather than written and dropped.
#:
#: ITS VALUE IS 100 AND NOTHING ELSE. The game does not read it: a cleanse has
#: no size. 100 is "all of it", as on a cooldown reset that states no chance,
#: so the table says the same thing for every such row.
CLEANSE_ACTION = "cleanse"

#: The actions that are NO ACTION AT ALL: each hangs a number on an ailment the
#: wearer applies, and names no event, because nothing fires it. Issue #1833,
#: ruled 2026-10-06, for the sentences that move a number ON AN ENEMY while it
#: carries an ailment: "Bleeding enemies take 20%-40% increased damage from all
#: sources". The 2026-09-14 entry ruled these are a modifier carried with the
#: ailment and not a condition on the attacker.
#:
#: THE AILMENT CELL NAMES THE AILMENT and is required; the value is the percent
#: the carrier's number moves by, above 0 and up to 100. When the wearer applies
#: that ailment to another character, as a new effect or a refresh, the game
#: reads these rows and puts them on the character struck, where they count
#: while it carries the ailment.
#: `UCataclysmAbilitySystemComponent::AilmentRiderNamed` holds the same names.
AILMENT_RIDER_ACTIONS = (
    # Added to the damage the carrier takes from every source.
    "ailment_damage_taken",
    # Added to the armour the carrier has had removed, clamped at 100 with the rest.
    "ailment_armor_removed",
    # Taken off what the carrier's attacks are worth.
    "ailment_damage_dealt",
    # Added to the carrier's healing received reduction, capped at 100.
    "ailment_healing_received",
    # Taken off both the carrier's movement speed and its attack speed.
    "ailment_speed",
    # Taken off the carrier's movement speed alone.
    "ailment_movement_speed",
    # HOW MANY more nearby enemies the ailment passes to when its carrier dies.
    # A count and not a percent; the same bound holds it, above 0 and up to 100.
    "ailment_spread_on_death",
    # THE INCREASE on what the ailment deals at once when its applier applies it
    # again. Read on the applier and not carried; on `DETONATING_AILMENTS` only.
    "ailment_detonates_when_reapplied",
    # HOW MANY enemies near the target the ailment also goes to when it is
    # applied. A count and not a percent; read on the applier and not carried.
    "ailment_spread_on_application",
    # Taken off the carrier's MAXIMUM HEALTH while it carries the ailment, and
    # given back when the ailment ends. Up to `MAX_AILMENT_MAX_HEALTH_REMOVED`
    # and not the 100 the others may reach.
    "ailment_max_health_removed",
)

#: The rider that lowers its carrier's maximum health, and the most it may take.
#: Ruled 2026-10-08 for "Enemies with Necrosis have 1%-2% less maximum health".
#: `UCataclysmAbilitySystemComponent::AilmentMaxHealthRiderAction` holds the
#: name and `MaxAilmentMaxHealthRemovedPercent` the same bound, which the game
#: holds a carried figure to as well.
#:
#: HALF AND NOT THE WHOLE, a sanity bound chosen by the writing session: the one
#: sentence asks for 2, and a row that took the whole maximum would leave a
#: creature at the 1 point the game holds any maximum to. A maximum lowered by
#: half or less is never nought, so nothing can die of the row.
AILMENT_MAX_HEALTH_ACTION = "ailment_max_health_removed"
MAX_AILMENT_MAX_HEALTH_REMOVED = 50.0

#: The rider that makes an ailment DETONATE, and the ailments it can be hung on.
#: Ruled 2026-10-06 for "Void splinter stacks detonate for 50%-100% increased
#: damage". The game reads it where a share of health ailment is applied
#: (`UCataclysmSkillEffects::ApplyShareOfHealthOverTime`) and nowhere else, so on
#: any other ailment the row would do nothing and is refused. And the other
#: riders are refused on these ailments, because that function hands the
#: character struck no riders.
AILMENT_DETONATION_ACTION = "ailment_detonates_when_reapplied"
DETONATING_AILMENTS = ("Void Splinter",)

#: The rider that passes an ailment on at a death, and the ailment it may name
#: BESIDE the five in `AILMENTS`. Ruled 2026-10-06 for "Void splinter stacks
#: spread to nearby enemies when the afflicted enemy dies". The other numbers
#: hung on an ailment stay refused on Void Splinter: no sentence asks for one.
AILMENT_SPREAD_ACTION = "ailment_spread_on_death"
SPREADING_SHARE_AILMENTS = ("Void Splinter",)

#: The action that REPEATS THE SKILL JUST USED, FREE, with its value as the chance
#: out of 100. Mechanism B2, ruled 2026-10-05: "Every skill use has a 5%-15%
#: chance to cast a second time for free".
#: `UCataclysmAbilitySystemComponent::RepeatSkillAction` holds the same name.
REPEAT_SKILL_ACTION = "repeat_skill"

#: The events a repeat may be written on: the ones that name a skill.
#: `skill_use` is every paid use but the basic attack; `attack_use` is every
#: paid use, the basic attack included. `basic_attack` carries no skill.
REPEAT_SKILL_EVENTS = ("skill_use", "attack_use")

#: The action that GAINS A KEPT STACK ON A CLOCK AND, ON A USE OF A SKILL, SPENDS
#: THEM ALL TO CAST THAT SKILL AGAIN a number of times drawn from 1 to the
#: stacks spent. Ruled 2026-10-12 under the owner's delegation, for "Shard of
#: Anarchy (10-Piece Bonus): Every 30 seconds, you gain a stack of 'Temporal
#: Flux.' When you use a skill, you consume all stacks of 'Temporal Flux' and
#: your skill is cast a random number of times. The bonus is permanent for the
#: rest of the dungeon".
#: `UCataclysmAbilitySystemComponent::KeptStacksRepeatSkillAction` holds the
#: same name.
#:
#: ONE ROW STATES ALL OF IT. Every Seconds is the period of the gain, 30. Scale
#: Max Steps is the most stacks held, 5. The Action Event is the kind of use
#: that spends them. The value is the share of its damage each extra cast
#: deals, in per cent, 100 for the whole. A kept stack has no lifetime, so
#: Stack Seconds is refused.
#:
#: AND THE AILMENT CELL IS WHAT THE SENTENCE CALLS THE STACKS, "Temporal Flux",
#: which the game shows above the player's bars with the count and the cap.
#: Ruled 2026-10-12. Required, and it must be a name the row's own sentence
#: writes between single quotes, so a name cannot be typed that the sentence
#: does not give. On this action the cell names nothing the game applies.
#:
#: THE PERIOD IS COUNTED IN SECONDS OF COMBAT THAT ARE CARRIED FROM ONE FIGHT TO
#: THE NEXT, ruled 2026-10-12, for this action alone. Every other timed row
#: counts whole periods of one combat.
#:
#: NOT ONE OF `SKILL_IN_HAND_ACTIONS`, AND ON PURPOSE. The value of each of
#: those is a chance, and this row has none: a use with a stack held always
#: spends.
KEPT_STACKS_REPEAT_SKILL_ACTION = "kept_stacks_repeat_skill"

#: The events whose use spends kept stacks: `skill_use`, and no other until a
#: sentence asks for one. It is every paid use of a skill but the basic
#: attack, which the ruling of 2026-10-12 leaves out. The clock that gains the
#: stacks is not an event of the row: it is the row's Every Seconds.
KEPT_STACKS_REPEAT_SKILL_EVENTS = ("skill_use",)

#: The action that GAINS A KEPT STACK ON ITS EVENT, with no clock, and while the
#: stacks hold their cap makes the wearer's NEXT ABILITY BUT THE BASIC ATTACK
#: COST NO MANA AND DEAL MORE DAMAGE, spending them all. Ruled 2026-10-17 (O7,
#: O8), for "Null Emperor (6-Piece Bonus): Every time you hit a silenced enemy,
#: you gain a stack of 'Void.' At 10 stacks, your next ability costs no mana and
#: deals 100% more damage". `UCataclysmAbilitySystemComponent::
#: KeptStacksFreeNextAbilityAction` holds the same name.
#:
#: THE VALUE IS THE "MORE", IN PER CENT, AS THE SENTENCE WRITES IT: 100 is twice
#: the damage. Scale Max Steps is the cap, and the stacks buy the ability only at
#: it. The Ailment cell is what the sentence calls the stacks, between single
#: quotes, as on `KEPT_STACKS_REPEAT_SKILL_ACTION`. A kept stack has no lifetime,
#: so Stack Seconds is refused; the gain is the event, so Every Seconds is too.
#: A ROW THAT COUNTS NEVER WAITS (ruled 2026-09-30), so no Trigger Cooldown.
KEPT_STACKS_FREE_NEXT_ABILITY_ACTION = "kept_stacks_free_next_ability"

#: The events that gain such a stack: `hit_dealt`, and no other until a
#: sentence asks for one.
KEPT_STACKS_FREE_NEXT_ABILITY_EVENTS = ("hit_dealt",)

#: The most a row of that action may say the ability deals more, in per cent.
MAX_FREE_NEXT_ABILITY_MORE_PERCENT = 1000.0

#: The actions that trigger a DIFFERENT skill the character holds, with the
#: value as the chance. Ruled 2026-10-06, for Spellblade's Will. The first
#: takes a held skill with a cooldown, free: "Your melee attacks have a 25%
#: chance to trigger an ability with a cooldown". The second takes a held
#: spell, which pays its cost and starts no cooldown: "Your melee attacks have
#: a 25% of triggering one of your spells".
#: `UCataclysmAbilitySystemComponent::TriggerHeldSkillAction` and
#: `TriggerHeldSpellAction` hold the same names.
TRIGGER_HELD_SKILL_ACTION = "trigger_held_skill"
TRIGGER_HELD_SPELL_ACTION = "trigger_held_spell"

#: The actions that roll, once for the use in hand, that the use deals no
#: damage. Ruled 2026-10-06. The value is the chance. The first is for every
#: skill the row's tags allow: "Strike skills have a 10%-20% chance to miss
#: entirely regardless of other stats". The second is for a skill with a
#: cooldown only: "Your cooldown abilities have a 25% chance to deal no
#: damage". `UCataclysmAbilitySystemComponent::UseNoDamageAction` and
#: `CooldownUseNoDamageAction` hold the same names.
USE_NO_DAMAGE_ACTIONS = ("use_no_damage", "cooldown_use_no_damage")

#: The action that rolls for a strike to hit every enemy within 3 metres of its
#: user. Ruled 2026-10-06: "Melee skills have a 10%-20% chance to hit all
#: enemies within 3 meters". The 3 is the engine's, since no column carries a
#: distance for an action.
#: `UCataclysmAbilitySystemComponent::UseHitsAllNearbyAction` holds the name.
USE_HITS_ALL_NEARBY_ACTION = "use_hits_all_nearby"

#: The actions that roll for a use to hit its own user and no enemy. The owner
#: decided on 2026-10-06 that a character's own skills may hurt them where a
#: row's sentence says so. The first deals the user the whole of the use's hit:
#: "Melee skills have a 10%-20% chance to hit you instead of the enemy". The
#: second deals half: "Spells have a 15%-25% chance to backfire dealing half
#: damage to you". `UCataclysmAbilitySystemComponent::UseHitsItsUserAction` and
#: `UseBackfiresAction` hold the same names.
USE_HITS_ITS_USER_ACTIONS = ("use_hits_its_user", "use_backfires")

#: The same two for a use that deals increased damage: "Your cooldown abilities
#: have a 5%-20% chance to deal 50%-200% increased damage". The engine has them
#: (`UseIncreasedDamageAction`, `CooldownUseIncreasedDamageAction`).
#:
#: THE ROW'S VALUE IS THE CHANCE AND ITS SCALE STEP AND SCALE STEP HIGH ARE THE
#: SIZE OF THE INCREASE, the sentence's second range. ON THESE TWO ACTIONS THAT
#: PAIR IS A SIZE AND NOT A STEP: the row states no Scale and nothing steps.
#: Ruled 2026-10-06, so that the effect table gained no column for one
#: sentence. Both ranges roll from the item's one roll, so the top chance comes
#: with the top increase. The pair is required here and refused with a Scale.
USE_INCREASED_DAMAGE_ACTIONS = ("use_increased_damage",
                                "cooldown_use_increased_damage")

#: The most a use's rolled increase may be, in percent. A bound for typing
#: mistakes; the one sentence states 200.
MAX_USE_INCREASE = 1000.0

#: Every action that acts on the skill of the use in hand. Each is written on
#: an event that names a skill, and its value is a chance and nothing else.
SKILL_IN_HAND_ACTIONS = (REPEAT_SKILL_ACTION, TRIGGER_HELD_SKILL_ACTION,
                         TRIGGER_HELD_SPELL_ACTION) + USE_NO_DAMAGE_ACTIONS \
    + USE_INCREASED_DAMAGE_ACTIONS + (USE_HITS_ALL_NEARBY_ACTION,) \
    + USE_HITS_ITS_USER_ACTIONS

#: The action that makes an enemy the wearer kills EXPLODE FOR ITS OVERKILL, with
#: its value as the share of the overkill each enemy near the body takes, in
#: per cent. Ruled 2026-10-07: "Enemies killed by you explode for the overkill
#: amount", whose row states 100.
#: `UCataclysmAbilitySystemComponent::ExplodeVictimForOverkillAction` holds the
#: same name.
#:
#: ON `kill` AND NO OTHER EVENT. The event loop only records the share; the kill
#: site takes it once the loop has returned and makes the explosion with the
#: death's victim, place and overkill. No other event has a victim or an
#: overkill, so the row would be recorded and never taken.
OVERKILL_EXPLOSION_ACTION = "explode_victim_for_overkill"
OVERKILL_EXPLOSION_EVENTS = ("kill",)

#: The most an overkill explosion's share may be, in per cent. A bound for
#: typing mistakes, as `MAX_USE_INCREASE` is; the one sentence is the whole
#: overkill, 100.
MAX_OVERKILL_EXPLOSION_SHARE = 1000.0

#: The action that DEALS A DIRECT HIT TO THE OTHER CHARACTER OF ITS EVENT, with
#: its value as the hit's size: a percentage of the wearer's attack damage.
#: Ruled 2026-10-07: "When you evade a ranged attack, throw an attack dealing
#: 20-70% of your attack damage at that enemy". The hit cannot be evaded and is
#: otherwise an ordinary hit of the wearer's.
#: `UCataclysmAbilitySystemComponent::StrikeTargetAction` holds the same name.
STRIKE_TARGET_ACTION = "strike_target"

#: The events a strike may hang on: every one the game raises with a LIVING
#: CHARACTER OTHER THAN THE WEARER as its other character. The enemy the wearer
#: struck, the enemy a deployable struck, the attacker retaliation was paid to,
#: and the attacker whose blow was blocked, taken in melee or evaded.
#: `tools/tests/test_pool_action_names_match_the_engine.py` reads the calls of
#: `ActOnEvent` and fails if a name here is raised with no character, or if the
#: game raises an event with a character that is neither here nor in
#: `EVENTS_WHOSE_CHARACTER_CANNOT_BE_STRUCK`.
STRIKE_TARGET_EVENTS = (
    "hit_dealt",
    "first_hit_dealt",
    "critical_strike",
    "deployable_hit",
    "retaliation_dealt",
    "block",
    "melee_hit_taken",
    "dodge",
    # THE ATTACKER OF A LANDED HIT THAT IS NOT A TICK, and of a big hit. The War
    # vow options' third slice, V3a, 2026-11-25, which made `hit_taken` name the
    # attacker for Thornwall and raised `big_hit_taken` with it: a living
    # character other than the wearer, as `melee_hit_taken`'s is. No row strikes
    # on either today.
    "hit_taken",
    "big_hit_taken",
)

#: The events that carry a character a strike cannot land on, each with why.
#: `kill` and `afflicted_death` carry an enemy that has just died, and the
#: action does not strike the dead. `gadget_destroyed` carries the wearer's own
#: machine.
EVENTS_WHOSE_CHARACTER_CANNOT_BE_STRUCK = (
    "kill",
    "afflicted_death",
    "gadget_destroyed",
    # Ruled 2026-10-19 (M2, M5). `trap_placed` carries the wearer's own trap.
    # `trap_triggered` carries the enemy that set the trap off, which the
    # trap's cast has just struck and may have killed; NO STRIKE ON IT IS
    # RULED, so a strike row on it is refused here until a layer rules one and
    # moves it to `STRIKE_TARGET_EVENTS`, whose eight a merged test pins.
    "trap_placed",
    "trap_triggered",
    # The Death trees' slice C6d1 (2026-11-25). `minion_hit` carries the enemy a
    # minion's blow landed on; Cold Hands and Grave Cold apply Necrosis to it,
    # and NO STRIKE ON IT IS RULED, as for `trap_triggered` above.
    "minion_hit",
    # AND `minion_death` (the Death trees' slice C6d2, 2026-11-25), which carries
    # the commander's own follower as it dies: nothing a row may strike.
    "minion_death",
    # Slice C of plan_death_and_war.md, 2026-11-24. `gadget_placed` carries the
    # wearer's own gadget, as `trap_placed` carries its trap.
    "gadget_placed",
    # The Saboteur's device conditions, 2026-11-25. `device_placed` carries the
    # wearer's own trap or gadget.
    "device_placed",
    # V4a, 2026-11-25. `trap_armed` carries the wearer's own trap.
    "trap_armed",
    # V5b1, 2026-11-25. `gadget_expired` carries the wearer's own gadget.
    "gadget_expired",
)

#: The most a strike may be, in per cent of attack damage. A bound for typing
#: mistakes, the figure `MAX_NEARBY_PERCENT` gives a nearby smite; the one
#: sentence is 20 to 70.
MAX_STRIKE_TARGET_PERCENT = 1000.0

#: The action that EXECUTES THE ENEMY A HIT LEAVES BELOW A SHARE OF ITS HEALTH,
#: with its value as the chance, where 100 is always. Ruled 2026-10-12 under the
#: owner's delegation: "Strike skills have a 10%-20% chance to execute enemies
#: below 15% HP". The enemy loses the health it has left, with no damage formula,
#: and the death is the wearer's kill. Never a Boss or a Cataclysm Boss, and
#: nothing is done to a creature that takes no damage; the engine holds both
#: inside the action.
#: `UCataclysmAbilitySystemComponent::ExecuteTargetAction` holds the same name.
#:
#: THE SHARE IS THE ROW'S CONDITION VALUE, and the Condition must be
#: `EXECUTE_TARGET_CONDITION`. An action carries one condition, and on this
#: action it is the share of health and nothing else. The engine does not judge
#: it when the event is raised, which is before the hit's damage is written; it
#: judges it on the health the hit leaves.
EXECUTE_TARGET_ACTION = "execute_target"

#: The one condition an execute row states, and must state.
EXECUTE_TARGET_CONDITION = "target_health_below"

#: The events an execute may hang on: `hit_dealt` and no other. The engine acts
#: on the row's request where a hit's damage is written to the enemy's health,
#: and only `hit_dealt` is raised from inside that step with the enemy struck
#: and what reached its health.
EXECUTE_TARGET_EVENTS = ("hit_dealt",)


def takes_a_trigger_cooldown(action: str) -> bool:
    """Whether an action row MAKES SOMETHING HAPPEN, and so may wait between
    firings: a pool moved, a cooldown reset or reduced, or a random damage
    over time. Ruled 2026-09-30: a row that counts or stacks, and a next-use
    charge, states a count per event and never waits."""
    return (action in POOL_ACTIONS or action in COOLDOWN_RESET_ACTIONS
            or action in COOLDOWN_REDUCE_ACTIONS or action == RANDOM_DOT_ACTION
            or action == HEALTH_CAP_ACTION or action in NEARBY_ACTIONS
            or action in REMAINING_DAMAGE_ACTIONS or action in ALL_APPLY_STATUS_ACTIONS
            or action == DAMAGE_IMMUNITY_ACTION or action == REFLECT_BLOCKED_ACTION
            or action == TELEPORT_RANDOMLY_ACTION
            or action == BLAST_FROM_THE_DYING_ACTION
            or action == STUN_NEAR_THE_DYING_ACTION
            # A SLOW MAKES SOMETHING HAPPEN TOO. Traps layer 3, 2026-10-21: on
            # `trap_triggered`, which no hit fires, so it takes none by default.
            or action == SLOW_FOR_SECONDS_ACTION
            or action == STRIKE_TARGET_ACTION
            # COUNTERFIRE'S ANSWERS ARE SOMETHING HAPPENING: "No more than once
            # a second" is its Trigger Cooldown (V4b, 2026-11-25).
            or action == COUNTERFIRE_ACTION
            # AND TITANFALL'S "once every 30 seconds" IS ITS OWN (2026-11-25).
            or action == TITANFALL_ACTION
            # AN EXECUTE MAY STATE ONE, AND TAKES NONE BY DEFAULT: see
            # `ACTIONS_WITH_NO_DEFAULT_TRIGGER_COOLDOWN`. The engine starts a
            # stated wait when an enemy is executed, and not when the roll
            # alone comes up.
            or action == EXECUTE_TARGET_ACTION)


#: The actions that MAY STATE a Trigger Cooldown and are given NONE when the
#: cell is empty, on a hit-fired event as on any other. Ruled 2026-10-12 by the
#: coordinating session: "EXECUTE'S TRIGGER COOLDOWN: none. Each enemy a strike
#: leaves below the share rolls for itself." So the ruled row, whose cell is
#: empty, writes no wait, and one swing that leaves three enemies below the
#: share rolls for each of the three. A row that states a wait keeps it: the
#: engine already starts one at an execute, and a later sentence may state one.
ACTIONS_WITH_NO_DEFAULT_TRIGGER_COOLDOWN = (EXECUTE_TARGET_ACTION,)

#: What a percentage on an action row is a percentage OF.
#:
#: "Restore 5% of your maximum HP" and "drain 3% of your current HP" are both
#: authored, and they are different amounts on a hurt character. `maximum` is
#: the default because most sentences say maximum or say nothing at all.
FRACTION_BASES = (
    "maximum",
    "current",
    "event_amount",
    # THE VALUE ITSELF, A FLAT AMOUNT, ON THE CLASS RESOURCE ONLY. The Death
    # trees' slice C6b3 (2026-11-25), as ruled by the coordinating session:
    # Opening Found's "generates 1 Fervour per point", Reaper's Due's "gives 5
    # Fervour" and Tear Loose's "1 Fervour" are action rows, scaled by
    # `class_resource_generation` and paid through `UCataclysmFervour::RaisePool`
    # as every class resource gain is. War's per-event Fervour stats are kept.
    "points",
    "for_each_of_event_amount",
    "for_each_ten_of_event_amount",
)

#: The Fraction Of value that pays the row's value itself, and the one pool it
#: may move. The Death trees' slice C6b3 (2026-11-25).
FRACTION_OF_POINTS = "points"
CLASS_RESOURCE_POOL = "class_resource"

#: The two Fraction Of values that SIZE A ROW BY ITS EVENT'S AMOUNT rather than
#: naming a base: the value acts once for each whole unit, or each whole ten, of
#: what the event carried, on the pool's maximum. The Death trees' slice C3b
#: (2026-11-25): Feast, "heals 1% of maximum health for each Fervour spent";
#: Spirit to Mana and Ashen Ward, "1% ... per point for every 10 Fervour spent";
#: Soul Furnace, "10% of your weapon damage for each Fervour spent"; and Turn of
#: the Wheel, "Every 10 Fervour you spend takes 1 second off". Whole tens, so a
#: spend of 19 pays once: the coordinating session's ruling 3 of 2026-11-25.
#: `UCataclysmAbilitySystemComponent::ActOnEvent` multiplies the value before
#: any action reads it, so they are accepted on a pool, a nearby action and a
#: cooldown reduction, each on an event that carries an amount, and nowhere else.
FRACTION_BASES_SIZED_BY_THE_AMOUNT = (
    "for_each_of_event_amount",
    "for_each_ten_of_event_amount",
)

#: The events an action may hang on, DERIVED FROM THE CLOCK CONDITIONS rather
#: than written out again.
#:
#: Every clock is named `seconds_after_<event>` and asks "within N seconds of
#: X". An action names `<event>` and happens AT X. The two questions share one
#: list of events on purpose, so a clock added later cannot be missing here and
#: the two cannot drift apart. Issue #1833 argued for one vocabulary serving
#: both.
ACTION_EVENT_PREFIX = "seconds_after_"

#: The event of a row that grants on a clock rather than on something that
#: happened: "Every 10 seconds gain a stack of momentum". Issue #1833, timed
#: grants. The row's Every Seconds is the period, counted only while the
#: character is in combat (ruled 2026-09-24).
#:
#: NOT ONE OF `ACTION_ONLY_EVENTS`, AND ON PURPOSE.
#: `test_pool_action_names_match_the_engine.py` holds `action_events()` equal
#: to the events the game fires BY NAME through `ActOnEvent`. A timed row is
#: fired by the character's own step, each on its own period, so it has no
#: named call to be matched against. `granting_events()` accepts it.
TIMED_EVENT = "every_seconds"

#: The longest period a timed row may state, in seconds.
MAX_EVERY_SECONDS = 60.0

#: The action that GRANTS ITS WEARER A TEMPORARY ABSORB, with its value as the
#: amount: a percentage of the wearer's maximum health. The project owner,
#: 2026-10-07: it is separate from the energy shield. "Every 12 seconds gain a
#: shield absorbing 15%-25% of your maximum HP in damage" is the row. A grant
#: refreshes what is held to its own amount and never adds, and the absorb has
#: no duration (both ruled 2026-10-07).
#: `UCataclysmAbilitySystemComponent::TemporaryAbsorbAction` holds the same name.
#:
#: NOT ONE OF `POOL_ACTIONS`, AND ON PURPOSE. Each of those names two
#: attributes, what is held and the most that can be held, and the absorb is
#: one plain number with no maximum of its own.
TEMPORARY_ABSORB_ACTION = "temporary_absorb"

#: The action that PAYS A RETALIATION PULSE for a big hit: its value is the
#: share of the retaliation the big hit earns, paid to every enemy within its
#: Event Value in metres of the wearer, as retaliation is delivered. The War vow
#: options' third slice, V3a, 2026-11-25: Wrath Incarnate, "When a single hit
#: takes more than 10% of your Maximum HP, you retaliate against every enemy
#: within 4 metres for 200% of your retaliation damage". ON THE BIG HIT ONLY,
#: which is the one event carrying a blow to size it by.
#: `UCataclysmAbilitySystemComponent::RetaliationPulseAction` holds the same
#: name.
RETALIATION_PULSE_ACTION = "retaliation_pulse"
RETALIATION_PULSE_EVENTS = ("big_hit_taken",)
MAX_RETALIATION_PULSE_METRES = 20.0
MAX_RETALIATION_PULSE_PERCENT = 1000.0

#: The action that CHAINS AN ATTACK to the nearest enemies within its Event
#: Value in metres of the enemy struck, for its value's share of what the attack
#: sent before defences, to one enemy plus one for each kill in the current
#: Frenzy. The War vow options' third slice, second half, V3b, 2026-11-25:
#: Thousand Deaths, "Dual wield only. While in Frenzy, your attacks chain to one
#: additional nearby enemy for 60% damage. Each kill during Frenzy permanently
#: adds one additional chain target for the remainder of the Frenzy duration",
#: ruled 5 metres, and a chained blow does not chain again. ON `hit_dealt` ONLY,
#: the one event that names the enemy struck and carries what the attack sent.
#: `UCataclysmAbilitySystemComponent::FrenzyChainAction` holds the same name.
FRENZY_CHAIN_ACTION = "frenzy_chain"
FRENZY_CHAIN_EVENTS = ("hit_dealt",)
MAX_FRENZY_CHAIN_METRES = 20.0
MAX_FRENZY_CHAIN_PERCENT = 1000.0

#: The action that TAUNTS EVERY ENEMY within its Event Value in metres of the
#: trap its event names, for its value in seconds, through the engine's taunt
#: with all its refusals. The War vow options' fourth slice, V4a, 2026-11-25:
#: Baited, "When one of your traps arms, every enemy within 8 metres of it is
#: drawn to it: it goes for the trap instead of whatever it was attacking, for 4
#: seconds or until the trap goes off". ON `trap_armed` ONLY.
#: `UCataclysmAbilitySystemComponent::TauntEnemiesNearTrapAction` holds the
#: same name.
TAUNT_NEAR_TRAP_ACTION = "taunt_enemies_near_trap"
TAUNT_NEAR_TRAP_EVENTS = ("trap_armed",)
MAX_TAUNT_NEAR_TRAP_METRES = 20.0
MAX_TAUNT_NEAR_TRAP_SECONDS = 30.0

#: The action that PLACES THE LAST SKILL TRAP AGAIN where the movement began,
#: armed at once, through the ordinary placement. V4a, 2026-11-25: Trail of
#: Wire, "Each time you use your movement skill, the last trap you placed is
#: placed again where the movement began, armed at once". ON `movement_skill`
#: ONLY. ITS VALUE IS 100, always, as a cooldown reset's is: the sentence states
#: no chance. `UCataclysmAbilitySystemComponent::PlaceLastTrapAgainAction` holds
#: the same name.
PLACE_LAST_TRAP_AGAIN_ACTION = "place_last_trap_again"
PLACE_LAST_TRAP_AGAIN_EVENTS = ("movement_skill",)

#: The action that ANSWERS AN EVADED BLOW with the wearer's traps and gadgets:
#: one for every Event Value of them, rounded down, the nearest the attacker
#: first; a skill trap goes off, a skill gadget fires at the attacker at the
#: row's value as a share of its shot, and a War machine deals its own blow at
#: the attacker. The War vow options' fourth slice, second half, V4b,
#: 2026-11-25: Counterfire, "Each hit you evade is answered by one of your traps
#: or gadgets for every 2 you have on the battlefield, nearest the attacker
#: first: a gadget fires at it, a trap goes off. No more than once a second."
#: ON `dodge` ONLY, which names the attacker; the second is the row's Trigger
#: Cooldown. `UCataclysmAbilitySystemComponent::CounterfireAction` holds the
#: same name.
COUNTERFIRE_ACTION = "counterfire"
COUNTERFIRE_EVENTS = ("dodge",)

#: The action that LEAVES A COPY OF THE LAST SKILL TRAP PLACED where an enemy
#: the wearer killed fell, when one of the wearer's devices stands within its
#: Event Value in metres: armed at once, reserving 1 Fervour, lasting the row's
#: value in seconds. The War vow options' fifth slice, first half, V5a,
#: 2026-11-25: Where They Fall. ON `kill` ONLY.
#: `UCataclysmAbilitySystemComponent::CopyLastTrapAtKillAction` holds the name.
COPY_LAST_TRAP_AT_KILL_ACTION = "copy_last_trap_at_kill"
COPY_LAST_TRAP_AT_KILL_EVENTS = ("kill",)
MAX_COPY_LAST_TRAP_AT_KILL_METRES = 20.0
MAX_COPY_LAST_TRAP_AT_KILL_SECONDS = 30.0

#: The action that REPEATS THE HIT DEALT from each of the wearer's devices
#: within its Event Value in metres of the enemy struck, the nearest 15, at the
#: row's value in per cent of what the hit sent. V5a, 2026-11-25: Crossfire. ON
#: `hit_dealt` ONLY. `UCataclysmAbilitySystemComponent::CrossfireAction` holds
#: the name.
CROSSFIRE_ACTION = "crossfire"
CROSSFIRE_EVENTS = ("hit_dealt",)
MAX_CROSSFIRE_METRES = 30.0
MAX_CROSSFIRE_PERCENT = 100.0

#: The action that LEAVES A SCRAP TRAP where a gadget that was destroyed or
#: expired stood: a copy of the gadget marked a trap, firing the gadget's attack
#: once at the row's value in per cent at the first enemy within its Event Value
#: in metres. The War vow options' fifth slice, second half, first part, V5b1,
#: 2026-11-25: Scrap Mines. ON `gadget_destroyed` AND `gadget_expired` ONLY,
#: one row on each. `UCataclysmAbilitySystemComponent::ScrapMineAction` holds the
#: name.
SCRAP_MINE_ACTION = "scrap_mine"
SCRAP_MINE_EVENTS = ("gadget_destroyed", "gadget_expired")
MAX_SCRAP_MINE_METRES = 10.0
MAX_SCRAP_MINE_PERCENT = 1000.0

#: The action that, when a big hit took more than its Event Value in per cent of
#: the wearer's maximum health (the figure `big_hit_taken` carries: what got
#: through, of all four pools, after defences), leaves the wearer unkillable for
#: the row's value in seconds and stores that figure for the next attack. Its
#: own 30 seconds between firings are the row's Trigger Cooldown. The Bulwark's
#: Titanfall, ruled 2026-11-25 (T1-T6) on the sentence R6. ON `big_hit_taken`
#: ONLY. `UCataclysmAbilitySystemComponent::TitanfallAction` holds the name.
TITANFALL_ACTION = "titanfall"
TITANFALL_EVENTS = ("big_hit_taken",)
MAX_TITANFALL_SECONDS = 10.0
MAX_TITANFALL_SHARE_PERCENT = 100.0
MAX_COUNTERFIRE_DEVICES_PER_ANSWER = 10.0
MAX_COUNTERFIRE_PERCENT = 1000.0

#: The events a temporary absorb may be granted on: the clock, and no other
#: until a sentence asks for one. The clock counts only while the wearer is in
#: combat, which is what the ruling of 2026-10-07 asks of this row.
TEMPORARY_ABSORB_EVENTS = (TIMED_EVENT,)

#: The most a temporary absorb may be, in per cent of maximum health. A bound
#: for typing mistakes; the one sentence is 15 to 25.
MAX_TEMPORARY_ABSORB_PERCENT = 100.0

#: Events an action may hang on that have NO clock of their own.
#:
#: HAVING NO CLOCK IS THE ONLY THING THAT PUTS AN EVENT HERE. An event with a
#: clock is derived from the clock's name by `action_events`, and listing it
#: here too is refused by a test. Most clocks are something done to the
#: character, but not all: `striking_a_boss`, `movement_skill` and `kill` are
#: things it did. `kill` was on this list until the Death trees' kill generator
#: (2026-11-24) gave it the clock `seconds_after_kill`, and is derived since;
#: the game raises the event as it did.
#:
#: `hit_dealt` IS NOT `hit_taken`. They are the same word at opposite ends of
#: one blow. `seconds_after_hit_taken` is stamped on the character that was
#: hit; `hit_dealt` fires on the character that hit.
ACTION_ONLY_EVENTS = (
    "critical_strike",
    "nearby_death",
    "skill_use",
    # EVERY PAID USE, THE BASIC ATTACK INCLUDED, raised after `skill_use` with
    # the skill in hand. Ruled 2026-10-06, for "Your melee attacks have a
    # 12%-15% chance to trigger twice". A basic attack is asked with its own
    # tags and Type.Melee or Type.Ranged by its shape.
    "attack_use",
    # A PAID USE OF A HEALING SKILL: a skill carrying a tag under
    # `Stat.Recovery`, which are Living Pyre and Blood Pyre. Ruled 2026-10-10,
    # for "Using a healing skill grants 10%-20% increased damage for 5
    # seconds". Raised by `UCataclysmAbilitySystemComponent::ActOnSkillUse`
    # after `skill_use` and never for the basic attack. It is an event of its
    # own because a row scaled by `own_stacks` is granted on every event of its
    # name whatever Required Tags it states, so such a row on `skill_use` would
    # be granted by every skill. NOT IN `REPEAT_SKILL_EVENTS`: `skill_use`
    # already serves a row that repeats the skill used.
    "healing_skill_use",
    "hit_dealt",
    # APPLYING A DAMAGE OVER TIME TO ANOTHER CHARACTER, a refresh included.
    # Issue #1833, for "Applying a DoT to an enemy grants 5%-10% increased
    # damage for 4 seconds, stacking up to 5 times". Raised in
    # `UCataclysmSkillEffects::ApplyDamageOverTime` on the applier.
    "dot_applied",
    # A DEPLOYABLE MACHINE'S BLOW, raised on its summoner with the machine's
    # tags and the enemy struck. Issue #1833, deployable Part 2, ruled
    # 2026-09-25: a new event rather than `hit_dealt`, whose unscoped rows
    # would otherwise begin firing on every machine's blow.
    "deployable_hit",
    # RETALIATION PAID TO ONE ATTACKER, raised on the retaliating character
    # once for each target in `UCataclysmRetaliation::Pay`, with that target
    # and what reached its health. Issue #1833 group D: "Your retaliation
    # damage also applies a random DoT to attackers".
    "retaliation_dealt",
    # HEALTH FALLING BELOW A SHARE OF ITS MAXIMUM, raised by
    # `UCataclysmAbilitySystemComponent::NoteHealthForCrossing` on every drop,
    # carrying the share held before; each row names its threshold in Event
    # Value. Issue #1833 group D part 2: Archon's Aegis and the Demon King's
    # Regalia.
    "health_falls_below",
    # A DUNGEON FLOOR STARTING, raised on the player in `StartPlay` and
    # `GoToFloor` after the floor's rules reach it. Issue #1833 group D part 2:
    # "You start every dungeon floor at 30%-50% of your maximum HP".
    "floor_start",
    # A BLOW TAKING THE ENERGY SHIELD FROM SOMETHING TO NOTHING, raised by
    # `UCataclysmAbilitySystemComponent::NoteEnergyShieldBroken` from the
    # damage branch of `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`.
    # Issue #1833 group D part 3: "When your energy shield is broken, you smite
    # all nearby enemies". A drain or a reservation that empties it is no break.
    "energy_shield_broken",
    # THE PLAYER DYING, raised once per death in
    # `ACataclysmPlayerCharacter::HandleDeath` after the death is marked. Issue
    # #1833 group D part 3: "On death all nearby enemies are healed for 10%-20%
    # of their maximum HP".
    "player_death",
    # A GADGET KILLED, raised on its summoner in `ACataclysmMinion::HandleDeath`
    # for a minion that is `Type.Deployable`, traps included, naming the gadget.
    # Issue #1833 group D part 5, ruled 2026-10-01: "destroyed" means killed, so
    # a lifespan running out, or the summon cap making room, raises nothing.
    "gadget_destroyed",
    # A RESOURCE CONSUMED: a class-resource cost paid, once per payment, or a
    # next-use charge spent, once per charge. Issue #1833 group D part 5, ruled
    # 2026-10-01. Mana is not a resource here, and the Fervour mechanics' own
    # spends raise nothing.
    "resource_consumed",
    # THE ENERGY SHIELD FILLED BY REGENERATION, raised by
    # `UCataclysmAbilitySystemComponent::NoteEnergyShieldRecharged` from
    # `UCataclysmRegeneration::ApplyStep` when a step takes the shield from below
    # its maximum to it. Issue #1833 group E part 2: "When your energy shield
    # fully recharges, release a nova".
    "energy_shield_recharged",
    # THE WEARER'S FIRST BLOW TO GET THROUGH TO AN ENEMY, raised by the player
    # character beside `hit_dealt` with the same tags, the target and what
    # reached its health. Issue #1833 group E part 1, ruled 2026-10-01: "Your
    # first hit against each enemy". Not `hit_dealt` with
    # `target_not_yet_struck_by_you`, because the target records the blow
    # before it is announced, so that condition is false on every hit by then.
    "first_hit_dealt",
    # A SKILL'S ABILITY HAS ENDED WITHOUT BEING CANCELLED, with the skill's own
    # tags. Ruled 2026-10-06, for "After using a charge skill you are briefly
    # stunned": `skill_use` is raised when the skill is paid, before a charge
    # has moved. `UCataclysmAbilitySystemComponent::SkillEndEvent`.
    "skill_end",
    # AN ENEMY DYING WHILE IT CARRIES ONE OF THE WEARER'S DAMAGE OVER TIME
    # AILMENTS, raised on whoever applied the ailment with the dead enemy as the
    # target, its maximum health as the amount and that character's ailments on
    # the body as the tags. Ruled 2026-10-07, for "Plague Doctor (10-Piece
    # Bonus): When an enemy dies while affected by a DoT from you". Raised in
    # `UCataclysmContagion::AnnounceAfflictedDeath`.
    "afflicted_death",
    # A PAID USE WHOSE TAGS HOLD `Type.Melee`, THE BASIC ATTACK INCLUDED, raised
    # by `UCataclysmAbilitySystemComponent::ActOnSkillUse` after `attack_use`.
    # Ruled 2026-10-17 (R1) for Spellblade's Will's six-piece bonus, "When you
    # use a melee attack, you gain a stack of Spellslinger": an own stack is
    # granted on every event of its name whatever Required Tags it states, so
    # such a row on `attack_use` would be granted by spells too. An attack event
    # and not a skill event. NOT IN `REPEAT_SKILL_EVENTS`: a row that repeats a
    # melee use already names `attack_use` with `Type.Melee`.
    "melee_attack_use",
    # A LETHAL BLOW OR TICK REPRIEVED, raised once per reprieve by
    # `UCataclysmAbilitySystemComponent::NoteLethalHitReprieved` from
    # `UCataclysmVitalAttributeSet::PostGameplayEffectExecute`, with no tags,
    # amount or character. Ruled 2026-10-18 for the Demon King's Regalia's
    # ten-piece, "instead gain 100% crit chance and crit multiplier ... for 3
    # seconds": two own-stack rows on it. NOT raised for Nothing Stops It's save,
    # so a wearer holding both gains the critical strike only from the reprieve.
    "lethal_hit_reprieved",
    # A SOUL TAKEN, raised once per Soul on its taker by
    # `ACataclysmReaperSoul::TakeSoulsNear`, with no tags, amount or character.
    # Ruled 2026-10-18 (R5, R6) for Reaper's Embrace's six-piece, "Picking up a
    # Soul grants a stack of Soul Strike that increases your damage by 10% for 5
    # seconds. Max 10 stacks": two own-stack rows on it. Raised at 10 stacks too.
    "soul_picked_up",
    # A SKILL TRAP PLACED, raised once per trap on its player by
    # `ACataclysmMinion::PlaceSkillTrap`, with the trap's type tags and the
    # trap as the character. Ruled 2026-10-19 (M2), for "Placing a trap
    # generates a burst of evasion" (a later layer's row). Clockless: placing
    # is something the player DID. In no narrower list yet; the layer that
    # writes the row says which action it takes.
    "trap_placed",
    # A SKILL TRAP TRIGGERED, raised once per trigger on its player by
    # `ACataclysmMinion::TriggerSkillTrap` after its cast started and before it
    # is used up, with the trap's type tags and the enemy that set it off.
    # Ruled 2026-10-19 (M5, T13), for the rows of later layers on a trap's
    # trigger. Clockless, as the one above; in no narrower list yet.
    "trap_triggered",
    # A MINION'S LANDED BLOW, raised once on its summoner (an imp's in
    # `ACataclysmMinion`, a thrall's in `UCataclysmSkillEffects::ApplyHit`), with
    # the minion's tags and the enemy struck. The Death trees' slice C6d1
    # (2026-11-25), as ruled, for Cold Hands and Grave Cold. Not a machine's,
    # which is `deployable_hit`.
    "minion_hit",
    # A GADGET PLACED, raised once per gadget on its player by
    # `ACataclysmMinion::PlaceSkillGadget` and, for a War machine that is not
    # a trap, `UCataclysmDeployableSkill::DeployOne`, with the gadget's type
    # tags and the gadget as the character. Slice C of plan_death_and_war.md,
    # 2026-11-24 (`FCataclysmWarFervour::GadgetPlacedEvent`): nothing raised one
    # before, and Razor Instincts and Forward Operating Base are about it.
    # Clockless, as the two above; in no narrower list yet.
    "gadget_placed",
    # A TRAP OR A GADGET PLACED, one event for either, raised beside
    # `trap_placed` by `ACataclysmMinion::PlaceSkillTrap`, beside `gadget_placed`
    # by `PlaceSkillGadget`, and by `UCataclysmDeployableSkill::DeployOne` for
    # every War machine, a trap included. The Saboteur's device conditions,
    # 2026-11-25 (the coordinating session's ruling): Razor Instincts, "+3%
    # evasion chance per point for 3 seconds after placing a trap or gadget", is
    # one row on it, so one placing grants one stack. Clockless.
    "device_placed",
    # THE BERSERKER ENTERING FRENZY, raised once per entry by
    # `FCataclysmBerserkerStates::Judge` after the state is set, with no tags.
    # The War vow options' second slice, 2026-11-25, ruled that day: Opportunist,
    # "Entering Frenzy immediately resets the cooldown of your movement skill", is
    # a `cooldown_reset_movement` row on it. Clockless.
    "frenzy_entered",
    # A BIG HIT TAKEN: one landed hit, not a tick and not the wearer's own,
    # that took more than 10% of maximum health after defences, counting what
    # reached health, the energy shield, mana and the temporary absorb. Raised
    # by `UCataclysmAbilitySystemComponent::NoteBigHitTaken` with that amount
    # and the attacker. The War vow options' third slice, V3a, ruled 2026-11-25
    # ("the 10% fixed in the engine"): Wrath Incarnate's pulse is a
    # `retaliation_pulse` row on it. Clockless.
    "big_hit_taken",
    # A TRAP OF THE WEARER'S ARMED: a skill trap at its arming time, at its step,
    # by an arming action or at an arming time of nought, and a War machine trap
    # when it is placed. Raised by `ACataclysmMinion::AnnounceTrapArmed` with the
    # trap's tags, naming the trap, at depth nought. The War vow options' fourth
    # slice, V4a, ruled 2026-11-25: Baited's taunt is a
    # `taunt_enemies_near_trap` row on it. Clockless.
    "trap_armed",
    # A GADGET OF THE WEARER'S WHOSE LIFE SPAN RAN OUT, raised by
    # `ACataclysmMinion::LifeSpanExpired` with the gadget's tags, naming it,
    # before it goes. Ruled in the vow survey of 2026-11-25 ("a gadget expiring
    # raises an event") and built for Scrap Mines, V5b1. Clockless.
    "gadget_expired",
    # A DETONATION OF THE WEARER'S, raised by
    # `UCataclysmDetonation::NoteDetonation` after the blast, with the detonated
    # minion's tags and NO CHARACTER: a minion a skill, a release, a contact or
    # Mass Grave set off, and slice D's remains when they are detonated. The
    # finishing of the Death Cataclysm, slice C (2026-11-26), for Bursting
    # Ranks' "for each detonation of yours in the last 2 seconds, of a minion or
    # of remains". Clockless.
    "minion_detonated",
)

#: The events a `consecutive_hits` row may count: the ones that name who was
#: struck. Issue #1833, phase 2. `hit_dealt` alone today, raised by the player
#: character with the blow's target.
CONSECUTIVE_HIT_EVENTS = (
    "hit_dealt",
)

#: The events that carry an amount of their own, so a row may take a fraction
#: OF THAT AMOUNT rather than of a pool.
#:
#: A SHORT LIST ON PURPOSE. A row asking for a fraction of an amount on an
#: event that carries none would resolve to nothing and say so nowhere, which
#: is the silent failure this table keeps producing.
#:
#: `hit_dealt` AND `retaliation_dealt` JOINED ON 2026-10-05, issue #1833, for
#: "You take 10%-20% of the damage dealt by your own point blank AOE skills" and
#: the three drawbacks that pay a share of retaliation back to the wearer. Each
#: passes what reached the other character's HEALTH:
#: `ACataclysmPlayerCharacter` passes `Notice.DealtToHealth` and
#: `UCataclysmRetaliation::Pay` passes what its payment took.
#: `tools/tests/test_pool_action_names_match_the_engine.py` reads the calls and
#: fails if a name here is fired with no amount.
#:
#: `kill` AND `afflicted_death` JOINED ON 2026-10-07. Each passes the MAXIMUM
#: HEALTH of the enemy that died: `ACataclysmPlayerCharacter` passes the slain
#: enemy's, and `UCataclysmContagion::AnnounceAfflictedDeath` the dying one's.
#: So a row on either can be sized by the enemy that died.
EVENTS_WITH_AN_AMOUNT = (
    "health_cost",
    "hit_dealt",
    "retaliation_dealt",
    "kill",
    "afflicted_death",
    # THE FERVOUR ONE PAYMENT SPENT. The Death trees' slice C3a (2026-11-25),
    # raised by `UCataclysmAbilitySystemComponent::NoteFervourSpent`.
    "fervour_spent",
    # What the big hit took, after defences. V3a, 2026-11-25.
    "big_hit_taken",
)


def action_events() -> set[str]:
    """Every event name an action row may use: the clocks, and the five that
    have no clock.

    THE CLOCK HALF IS DERIVED RATHER THAN REPEATED, so a clock added later
    cannot be missing here. The other half is written out, because those events
    have no clock to derive from.
    """
    return {name[len(ACTION_EVENT_PREFIX):] for name in CONDITIONS
            if name.startswith(ACTION_EVENT_PREFIX)} | set(ACTION_ONLY_EVENTS)

def granting_events() -> set[str]:
    """Every event a row may grant or act on: the ones the game fires by
    name, and the timed one. Issue #1833."""
    return action_events() | {TIMED_EVENT}


def _check_pool_action(index: int, who: str, action: str, event: str,
                       fraction_of: str, kind: str, raw,
                       headers: dict[str, int]) -> None:
    """Everything an action row must say, and everything it must not.

    REFUSED RATHER THAN IGNORED, in every case. A row that validates and then
    grants nothing is the failure this table keeps producing, and it is silent:
    the sheet has a number in it, the generator writes it, the game reads a
    name it has no case for, and nothing anywhere says so.

    A SCALE IS STILL REFUSED. It sizes a stat's modifier by a state of the
    character, and an action has no stat, so a row carrying one would have it
    dropped without anything saying so.

    A CONDITION IS NO LONGER REFUSED. It was, while nothing judged it at the
    moment an action fires; the change that added the five clockless events
    judges it there, so "killing an enemy while below 30% HP" can be written.
    """
    if action in NEXT_USE_ACTIONS:
        _check_next_use_action(index, who, action, event, fraction_of, kind,
                               raw, headers)
        return
    if action in PLACED_ACTIONS:
        _check_placed_action(index, who, action, event, fraction_of, kind,
                             raw, headers)
        return
    if action in NTH_ACTIONS:
        _check_nth_action(index, who, action, event, fraction_of, kind,
                          raw, headers)
        return
    if action in COOLDOWN_RESET_ACTIONS or action in COOLDOWN_REDUCE_ACTIONS:
        _check_cooldown_reset_action(index, who, action, event, fraction_of,
                                     kind, raw, headers)
        return
    if action == RANDOM_DOT_ACTION:
        _check_random_dot_action(index, who, action, event, fraction_of, kind,
                                 raw, headers)
        return
    if action == HEALTH_CAP_ACTION:
        _check_health_cap_action(index, who, action, event, fraction_of, kind,
                                 raw, headers)
        return
    if action in NEARBY_ACTIONS:
        _check_nearby_action(index, who, action, event, fraction_of, kind,
                             raw, headers)
        return
    if action in REMAINING_DAMAGE_ACTIONS:
        _check_remaining_damage_action(index, who, action, event, fraction_of,
                                       kind, raw, headers)
        return
    if action in ALL_APPLY_STATUS_ACTIONS:
        _check_apply_status_action(index, who, action, event, fraction_of,
                                   kind, raw, headers)
        return
    if action == DAMAGE_IMMUNITY_ACTION:
        _check_damage_immunity_action(index, who, action, event, fraction_of,
                                      kind, raw, headers)
        return
    if action == TELEPORT_RANDOMLY_ACTION:
        _check_teleport_randomly_action(index, who, action, event, fraction_of,
                                        kind, raw, headers)
        return
    if action == GADGETS_CAST_NOW_ACTION:
        _check_gadgets_cast_now_action(index, who, action, event, fraction_of,
                                       kind, raw, headers)
        return
    if action in SKILL_IN_HAND_ACTIONS:
        _check_repeat_skill_action(index, who, action, event, fraction_of,
                                   kind, raw, headers)
        return
    if action == REFLECT_BLOCKED_ACTION:
        _check_reflect_blocked_action(index, who, action, event, fraction_of,
                                      kind, raw, headers)
        return
    if action == BLAST_FROM_THE_DYING_ACTION:
        _check_blast_from_the_dying_action(index, who, action, event, fraction_of,
                                           kind, raw, headers)
        return
    if action == STUN_NEAR_THE_DYING_ACTION:
        _check_stun_near_the_dying_action(index, who, action, event,
                                          fraction_of, kind, raw, headers)
        return
    if action == CLEANSE_ACTION:
        _check_cleanse_action(index, who, action, event, fraction_of, kind,
                              raw, headers)
        return
    if action == OVERKILL_EXPLOSION_ACTION:
        _check_overkill_explosion_action(index, who, action, event,
                                         fraction_of, kind, raw, headers)
        return
    if action == STRIKE_TARGET_ACTION:
        _check_strike_target_action(index, who, action, event, fraction_of,
                                    kind, raw, headers)
        return
    if action == EXECUTE_TARGET_ACTION:
        _check_execute_target_action(index, who, action, event, fraction_of,
                                     kind, raw, headers)
        return
    if action == TEMPORARY_ABSORB_ACTION:
        _check_temporary_absorb_action(index, who, action, event, fraction_of,
                                       kind, raw, headers)
        return
    if action == RETALIATION_PULSE_ACTION:
        _check_retaliation_pulse_action(index, who, action, event, fraction_of,
                                        kind, raw, headers)
        return
    if action == FRENZY_CHAIN_ACTION:
        _check_frenzy_chain_action(index, who, action, event, fraction_of,
                                   kind, raw, headers)
        return
    if action in (TAUNT_NEAR_TRAP_ACTION, PLACE_LAST_TRAP_AGAIN_ACTION,
                  COUNTERFIRE_ACTION, COPY_LAST_TRAP_AT_KILL_ACTION,
                  CROSSFIRE_ACTION, SCRAP_MINE_ACTION, TITANFALL_ACTION):
        _check_v4a_action(index, who, action, event, fraction_of, kind, raw,
                          headers)
        return
    if action == KEPT_STACKS_REPEAT_SKILL_ACTION:
        _check_kept_stacks_repeat_skill_action(index, who, action, event,
                                               fraction_of, kind, raw, headers)
        return
    if action == KEPT_STACKS_FREE_NEXT_ABILITY_ACTION:
        _check_kept_stacks_free_next_ability_action(index, who, action, event,
                                                    fraction_of, kind, raw,
                                                    headers)
        return
    if action in AILMENT_RIDER_ACTIONS:
        _check_ailment_rider_action(index, who, action, event, fraction_of,
                                    kind, raw, headers)
        return
    if action == SLOW_FOR_SECONDS_ACTION:
        _check_slow_for_seconds_action(index, who, action, event, fraction_of,
                                       kind, raw, headers)
        return
    if action == KILLING_BLOW_EXPLOSION_ACTION:
        _check_killing_blow_explosion_action(index, who, action, event,
                                             fraction_of, kind, raw, headers)
        return
    if action == SLAIN_BURST_ACTION:
        _check_slain_burst_action(index, who, action, event, fraction_of,
                                  kind, raw, headers)
        return
    if action == BLEED_LEFT_TO_STRONGEST_ACTION:
        _check_bleed_left_to_strongest_action(index, who, action, event,
                                              fraction_of, kind, raw, headers)
        return
    if action == NEXT_HIT_CRITICAL_ACTION:
        _check_next_hit_critical_action(index, who, action, event,
                                        fraction_of, kind, raw, headers)
        return
    if action == FERVOUR_HOLD_EXTENDED_ACTION:
        _check_fervour_hold_extended_action(index, who, action, event,
                                            fraction_of, kind, raw, headers)
        return
    if action == NEXT_HIT_CRITICAL_TWICE_ACTION:
        _check_next_hit_critical_twice_action(index, who, action, event,
                                              fraction_of, kind, raw, headers)
        return
    if action in STRONGEST_NEAR_ACTIONS:
        _check_strongest_near_action(index, who, action, event, fraction_of,
                                     kind, raw, headers)
        return
    if action in TRAPS_NEARBY_ACTIONS:
        _check_traps_nearby_action(index, who, action, event, fraction_of,
                                   kind, raw, headers)
        return
    if action == GADGET_FIRES_AT_TRAP_ENEMY_ACTION:
        _check_gadget_fires_at_trap_enemy_action(index, who, action, event,
                                                 fraction_of, kind, raw,
                                                 headers)
        return
    if action not in POOL_ACTIONS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} moves the pool {action!r}, "
            f"which is not one the game has. Known: "
            f"{', '.join(POOL_ACTIONS)}; or a next-use charge, "
            f"{', '.join(NEXT_USE_ACTIONS)}; or a placed stack, "
            f"{', '.join(PLACED_ACTIONS)}; or an every-Nth action, "
            f"{', '.join(NTH_ACTIONS)}; or a cooldown reset, "
            f"{', '.join(COOLDOWN_RESET_ACTIONS)}; or a cooldown reduction, "
            f"{', '.join(COOLDOWN_REDUCE_ACTIONS)}; or {RANDOM_DOT_ACTION}; "
            f"or {HEALTH_CAP_ACTION}; or a nearby action, "
            f"{', '.join(NEARBY_ACTIONS)}; or a remaining damage action, "
            f"{', '.join(REMAINING_DAMAGE_ACTIONS)}; or a status action, "
            f"{', '.join(ALL_APPLY_STATUS_ACTIONS)}; or {DAMAGE_IMMUNITY_ACTION}; "
            f"or {TELEPORT_RANDOMLY_ACTION}; "
            f"or {GADGETS_CAST_NOW_ACTION}; "
            f"or {REFLECT_BLOCKED_ACTION}; or {BLAST_FROM_THE_DYING_ACTION}; "
            f"or {STUN_NEAR_THE_DYING_ACTION}; "
            f"or {SLOW_FOR_SECONDS_ACTION}; "
            f"or {CLEANSE_ACTION}; "
            f"or {OVERKILL_EXPLOSION_ACTION}; "
            f"or {KILLING_BLOW_EXPLOSION_ACTION}; "
            f"or {SLAIN_BURST_ACTION}; "
            f"or {BLEED_LEFT_TO_STRONGEST_ACTION}; "
            f"or {NEXT_HIT_CRITICAL_ACTION}; "
            f"or {FERVOUR_HOLD_EXTENDED_ACTION}; "
            f"or {NEXT_HIT_CRITICAL_TWICE_ACTION}; "
            f"or {', '.join(STRONGEST_NEAR_ACTIONS)}; "
            f"or an action on the wearer's other traps, "
            f"{', '.join(TRAPS_NEARBY_ACTIONS)}; "
            f"or {GADGET_FIRES_AT_TRAP_ENEMY_ACTION}; "
            f"or {STRIKE_TARGET_ACTION}; "
            f"or {EXECUTE_TARGET_ACTION}; "
            f"or {TEMPORARY_ABSORB_ACTION}; "
            f"or {KEPT_STACKS_REPEAT_SKILL_ACTION}; "
            f"or {KEPT_STACKS_FREE_NEXT_ABILITY_ACTION}; "
            f"or a skill-in-hand action, {', '.join(SKILL_IN_HAND_ACTIONS)}; "
            f"or a rider on an ailment, {', '.join(AILMENT_RIDER_ACTIONS)}.")

    known = granting_events()
    if not event:
        raise DataError(
            f"Enchantment Effects row {index}: {who} moves a pool and names no "
            f"event to move it on. An action happens AT something. Known: "
            f"{', '.join(sorted(known))}.")
    if event not in known:
        raise DataError(
            f"Enchantment Effects row {index}: {who} moves a pool on the event "
            f"{event!r}, which the game does not record. Known: "
            f"{', '.join(sorted(known))}.")

    if fraction_of and fraction_of not in FRACTION_BASES:
        raise DataError(
            f"Enchantment Effects row {index}: {who} takes its percentage of "
            f"{fraction_of!r}. It is of {' or '.join(FRACTION_BASES)}, and an "
            f"empty column means maximum.")

    if kind:
        raise DataError(
            f"Enchantment Effects row {index}: {who} moves a pool and also has "
            f"the value kind {kind!r}. The three buckets multiply a stat and an "
            f"action has no stat, so the column must be empty on an action row.")

    if fraction_of == FRACTION_OF_POINTS and action != CLASS_RESOURCE_POOL:
        raise DataError(
            f"Enchantment Effects row {index}: {who} pays a flat amount "
            f"({FRACTION_OF_POINTS!r}) of the pool {action!r}. A flat amount is "
            f"of {CLASS_RESOURCE_POOL!r} only; every other pool is moved by a "
            f"share of a base.")

    if (fraction_of in ("event_amount", *FRACTION_BASES_SIZED_BY_THE_AMOUNT)
            and event not in EVENTS_WITH_AN_AMOUNT):
        raise DataError(
            f"Enchantment Effects row {index}: {who} takes a fraction of the "
            f"amount its event carried, and {event!r} carries no amount. The "
            f"events that do: {', '.join(EVENTS_WITH_AN_AMOUNT)}.")

    written = clean(_cell(raw, headers, "Scale"))
    if written:
        raise DataError(
            f"Enchantment Effects row {index}: {who} moves a pool and carries "
            f"the scale {written!r}. A scale sizes a stat's modifier and an "
            f"action has no stat, so it would be dropped without anything "
            f"saying so.")

def _check_placed_action(index: int, who: str, action: str, event: str,
                         fraction_of: str, kind: str, raw,
                         headers: dict[str, int]) -> None:
    """Everything a placed-stack row must say, and everything it must not.

    Issue #1833, phase 2. THE EVENT MUST NAME THE OTHER CHARACTER, because
    that is who the stack is placed on; the window and the cap are checked
    where Stack Seconds and Scale Max Steps are read. A fraction, a value kind
    and a scale each mean nothing here, so each is refused rather than
    dropped.
    """
    events = PLACED_ACTIONS[action]
    if event not in events:
        raise DataError(
            f"Enchantment Effects row {index}: {who} places {action} on the "
            f"event {event or '(none)'!r}. Only an event naming the character "
            f"it is placed on can: {', '.join(events)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} places {action} and "
                f"states {column} {written!r}. A placed stack takes away its "
                f"value per stack and nothing else, so the column must be "
                f"empty.")


def _check_random_dot_action(index: int, who: str, action: str, event: str,
                             fraction_of: str, kind: str, raw,
                             headers: dict[str, int]) -> None:
    """Everything a random damage over time row must say, and everything it
    must not. Issue #1833 group D.

    THE EVENT MUST NAME THE OTHER CHARACTER AND CARRY WHAT REACHED ITS HEALTH;
    see `RANDOM_DOT_EVENTS`. The chance is checked where the value is read. A
    fraction, a value kind and a scale each mean nothing here, so each is
    refused rather than dropped.
    """
    if event not in RANDOM_DOT_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} applies a random damage "
            f"over time on the event {event or '(none)'!r}. Only an event "
            f"naming the character struck and what reached its health can: "
            f"{', '.join(RANDOM_DOT_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} applies a random "
                f"damage over time and states {column} {written!r}. Its value "
                f"is a chance and nothing else, so the column must be empty.")


def _check_health_cap_action(index: int, who: str, action: str, event: str,
                             fraction_of: str, kind: str, raw,
                             headers: dict[str, int]) -> None:
    """Everything a health cap row must say, and everything it must not. Issue
    #1833 group D part 2. An event is required, because the cap happens AT
    something; the share is checked where the value is read. A fraction, a value
    kind and a scale each mean nothing here, so each is refused rather than
    dropped.
    """
    known = action_events()
    if event not in known:
        raise DataError(
            f"Enchantment Effects row {index}: {who} lowers health on the event "
            f"{event or '(none)'!r}, which the game does not fire. Known: "
            f"{', '.join(sorted(known))}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} lowers health to a "
                f"share of its maximum and states {column} {written!r}. Its "
                f"value is that share and nothing else, so the column must be "
                f"empty.")


def _sized_by_the_amount(index: int, who: str, event: str, fraction_of: str) -> str:
    """The Fraction Of a row has left to refuse, once one that sizes it by its
    event's amount has been judged: empty for either of
    `FRACTION_BASES_SIZED_BY_THE_AMOUNT` on an event that carries an amount, and
    unchanged for anything else, so the caller's own refusal still sees it. The
    Death trees' slice C3b.
    """
    if fraction_of not in FRACTION_BASES_SIZED_BY_THE_AMOUNT:
        return fraction_of
    if event not in EVENTS_WITH_AN_AMOUNT:
        raise DataError(
            f"Enchantment Effects row {index}: {who} is sized {fraction_of!r}, "
            f"and {event or '(none)'!r} carries no amount. The events that do: "
            f"{', '.join(EVENTS_WITH_AN_AMOUNT)}.")
    return ""


def _check_nearby_action(index: int, who: str, action: str, event: str,
                         fraction_of: str, kind: str, raw,
                         headers: dict[str, int]) -> None:
    """Everything a nearby action row must say, and everything it must not.
    Issue #1833 group D part 3. An event is required, because the smite or the
    heal happens AT something; the share is checked where the value is read. A
    fraction, a value kind and a scale each mean nothing here, so each is
    refused rather than dropped -- but for a share sized by the event's amount,
    Soul Furnace's "for each Fervour spent" (the Death trees' slice C3b).
    """
    known = action_events()
    if event not in known:
        raise DataError(
            f"Enchantment Effects row {index}: {who} acts on the characters "
            f"nearby on the event {event or '(none)'!r}, which the game does "
            f"not fire. Known: {', '.join(sorted(known))}.")
    fraction_of = _sized_by_the_amount(index, who, event, fraction_of)
    # A NOVA SIZED BY A HIT NEEDS AN EVENT CARRYING ONE. V2b, 2026-11-25.
    if action == SMITE_NEARBY_BY_HIT_ACTION and event not in SMITE_NEARBY_BY_HIT_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} deals a share of a hit on "
            f"the event {event!r}, which carries no hit. Only "
            f"{', '.join(SMITE_NEARBY_BY_HIT_EVENTS)} carry what a hit took "
            f"from health.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} acts on the characters "
                f"nearby and states {column} {written!r}. Its value is a share "
                f"and nothing else, so the column must be empty.")


def _check_remaining_damage_action(index: int, who: str, action: str, event: str,
                                   fraction_of: str, kind: str, raw,
                                   headers: dict[str, int]) -> None:
    """Everything a remaining damage row must say, and everything it must not.
    Issue #1833 group D part 4. An event is required, and on
    `dot_remaining_target` it must name the character struck; the share and the
    Ailment are checked where they are read. A fraction, a value kind and a scale
    each mean nothing here, so each is refused rather than dropped.
    """
    known = (REMAINING_DAMAGE_TARGET_EVENTS if action == "dot_remaining_target"
             else tuple(sorted(action_events())))
    if event not in known:
        raise DataError(
            f"Enchantment Effects row {index}: {who} deals remaining damage on "
            f"the event {event or '(none)'!r}, which {action} cannot use. Known: "
            f"{', '.join(known)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} deals remaining damage "
                f"and states {column} {written!r}. Its value is a share and "
                f"nothing else, so the column must be empty.")


def _check_apply_status_action(index: int, who: str, action: str, event: str,
                               fraction_of: str, kind: str, raw,
                               headers: dict[str, int]) -> None:
    """Everything a status row must say, and everything it must not. Issue
    #1833 group E part 1. The event must name the other character, because that
    is who the status is applied to; the value and the Ailment are checked where
    they are read. A fraction, a value kind and a scale each mean nothing here,
    so each is refused rather than dropped.
    """
    if action in APPLY_STATUS_TO_SELF_ACTIONS:
        # ON THE WEARER, so the event need name nobody. Ruled 2026-10-06.
        if event not in APPLY_STATUS_TO_SELF_EVENTS:
            raise DataError(
                f"Enchantment Effects row {index}: {who} lays a status on its "
                f"wearer on the event {event or '(none)'!r}. Known for that: "
                f"{', '.join(APPLY_STATUS_TO_SELF_EVENTS)}.")
        if (action == "apply_status_to_self_sized") != (event == "dot_applied"):
            raise DataError(
                f"Enchantment Effects row {index}: {who} uses {action!r} on "
                f"the event {event!r}. apply_status_to_self_sized lays the "
                f"ailment just applied to another, so it is written on "
                f"dot_applied and nothing else is.")
    elif event not in APPLY_STATUS_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} applies a status on the "
            f"event {event or '(none)'!r}. Only an event naming the character "
            f"it is applied to can: {', '.join(APPLY_STATUS_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} applies a status and "
                f"states {column} {written!r}. Its value is a chance or seconds "
                f"and nothing else, so the column must be empty.")


def _check_reflect_blocked_action(index: int, who: str, action: str, event: str,
                                  fraction_of: str, kind: str, raw,
                                  headers: dict[str, int]) -> None:
    """Everything a reflect row must say, and everything it must not. Issue
    #1833 group E part 3. The event must carry the attacker and what the block
    removed; the share is checked where the value is read. A fraction, a value
    kind and a scale each mean nothing here, so each is refused rather than
    dropped.
    """
    if event not in REFLECT_BLOCKED_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} reflects blocked damage on "
            f"the event {event or '(none)'!r}. Only an event carrying the "
            f"attacker and what the block removed can: "
            f"{', '.join(REFLECT_BLOCKED_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} reflects blocked damage "
                f"and states {column} {written!r}. Its value is a share and "
                f"nothing else, so the column must be empty.")


def _check_blast_from_the_dying_action(index: int, who: str, action: str,
                                       event: str, fraction_of: str, kind: str,
                                       raw, headers: dict[str, int]) -> None:
    """Everything a blast from the dying must say, and everything it must not.
    Ruled 2026-10-07. The event must name who died and carry its maximum
    health; the share is checked where the value is read. A fraction, a value
    kind and a scale each mean nothing here, so each is refused rather than
    dropped.
    """
    if event not in BLAST_FROM_THE_DYING_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} blasts from an enemy that "
            f"died on the event {event or '(none)'!r}. Only an event raised on "
            f"whoever afflicted the dead enemy, carrying their ailments on the "
            f"body, can: {', '.join(BLAST_FROM_THE_DYING_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} blasts from an enemy "
                f"that died and states {column} {written!r}. Its value is a "
                f"share and nothing else, so the column must be empty.")


def _check_stun_near_the_dying_action(index: int, who: str, action: str,
                                      event: str, fraction_of: str, kind: str,
                                      raw, headers: dict[str, int]) -> None:
    """Everything a stun near the dying must say, and everything it must not.
    Ruled 2026-10-08. The event must be raised on whoever afflicted the enemy
    that died and name that enemy; the chance is checked where the value is
    read and the seconds where Stack Seconds is. A fraction, a value kind and a
    scale each mean nothing here, so each is refused rather than dropped.
    """
    if event not in STUN_NEAR_THE_DYING_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} stuns the enemies near an "
            f"enemy that died on the event {event or '(none)'!r}. Only an event "
            f"raised on whoever afflicted the dead enemy, naming it, can: "
            f"{', '.join(STUN_NEAR_THE_DYING_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} stuns the enemies near "
                f"an enemy that died and states {column} {written!r}. Its value "
                f"is a chance and its Stack Seconds how long the stun lasts, "
                f"and nothing else, so the column must be empty.")


def _check_overkill_explosion_action(index: int, who: str, action: str,
                                     event: str, fraction_of: str, kind: str,
                                     raw, headers: dict[str, int]) -> None:
    """Everything an overkill explosion row must say, and everything it must
    not. Ruled 2026-10-07. ON `kill` ONLY, because that is the one event whose
    raiser takes what the row records; the share is checked where the value is
    read. A fraction, a value kind and a scale each mean nothing here, so each
    is refused rather than dropped.
    """
    if event not in OVERKILL_EXPLOSION_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} explodes a killed enemy "
            f"for its overkill on the event {event or '(none)'!r}. Only a kill "
            f"has a victim and an overkill: "
            f"{', '.join(OVERKILL_EXPLOSION_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} explodes a killed enemy "
                f"for its overkill and states {column} {written!r}. Its value is "
                f"a share of the overkill and nothing else, so the column must "
                f"be empty.")


def _check_temporary_absorb_action(index: int, who: str, action: str,
                                   event: str, fraction_of: str, kind: str,
                                   raw, headers: dict[str, int]) -> None:
    """Everything a temporary absorb row must say, and everything it must not.
    The project owner, 2026-10-07: the absorb is separate from the energy
    shield. Ruled the same day: its clock runs in combat only, which is the
    timed event, and its value is a percentage of maximum health; the size is
    checked where the value is read. A fraction, a value kind and a scale each
    mean nothing here, so each is refused rather than dropped.
    """
    if event not in TEMPORARY_ABSORB_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} grants a temporary absorb "
            f"on the event {event or '(none)'!r}. It is granted on a clock "
            f"that counts only in combat and on nothing else: "
            f"{', '.join(TEMPORARY_ABSORB_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} grants a temporary "
                f"absorb and states {column} {written!r}. Its value is a "
                f"percentage of maximum health and nothing else, so the "
                f"column must be empty.")


def _check_retaliation_pulse_action(index: int, who: str, action: str,
                                    event: str, fraction_of: str, kind: str,
                                    raw, headers: dict[str, int]) -> None:
    """Everything a retaliation pulse row must say, and everything it must not.
    The War vow options' third slice, V3a, 2026-11-25: Wrath Incarnate. On the
    big hit only, which carries the blow the pulse is sized by; the radius is
    checked where Event Value is read. A fraction, a value kind and a scale
    each mean nothing here, so each is refused rather than dropped.
    """
    if event not in RETALIATION_PULSE_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} pays a retaliation pulse "
            f"on the event {event or '(none)'!r}. Only a big hit carries a blow "
            f"to size it by: {', '.join(RETALIATION_PULSE_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} pays a retaliation "
                f"pulse and states {column} {written!r}. Its value is a share "
                f"of the retaliation the big hit earns and nothing else, so "
                f"the column must be empty.")


def _check_frenzy_chain_action(index: int, who: str, action: str,
                               event: str, fraction_of: str, kind: str,
                               raw, headers: dict[str, int]) -> None:
    """Everything a Frenzy chain row must say, and everything it must not. The
    War vow options' third slice, second half, V3b, 2026-11-25: Thousand
    Deaths. On `hit_dealt` only; the radius is checked where Event Value is
    read. A fraction, a value kind and a scale each mean nothing here, so each
    is refused rather than dropped.
    """
    if event not in FRENZY_CHAIN_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} chains an attack on the "
            f"event {event or '(none)'!r}. Only a hit dealt names the enemy "
            f"struck and what the attack sent: {', '.join(FRENZY_CHAIN_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} chains an attack and "
                f"states {column} {written!r}. Its value is a share of what the "
                f"attack sent and nothing else, so the column must be empty.")


def _check_v4a_action(index: int, who: str, action: str, event: str,
                      fraction_of: str, kind: str, raw,
                      headers: dict[str, int]) -> None:
    """Everything a Baited taunt row or a Trail of Wire row must say, and
    everything it must not. The War vow options' fourth slice, V4a, 2026-11-25.
    Each on its one event; the radius, the seconds and the value are checked
    where they are read. A fraction, a value kind and a scale each mean nothing
    here, so each is refused rather than dropped.
    """
    # AND COUNTERFIRE'S, ON `dodge`, since the fourth slice's second half,
    # V4b, the same day.
    events = {TAUNT_NEAR_TRAP_ACTION: TAUNT_NEAR_TRAP_EVENTS,
              PLACE_LAST_TRAP_AGAIN_ACTION: PLACE_LAST_TRAP_AGAIN_EVENTS,
              COUNTERFIRE_ACTION: COUNTERFIRE_EVENTS,
              # AND WHERE THEY FALL'S AND CROSSFIRE'S, V5a, the same day.
              COPY_LAST_TRAP_AT_KILL_ACTION: COPY_LAST_TRAP_AT_KILL_EVENTS,
              CROSSFIRE_ACTION: CROSSFIRE_EVENTS,
              # AND SCRAP MINES', V5b1, the same day.
              SCRAP_MINE_ACTION: SCRAP_MINE_EVENTS,
              # AND TITANFALL'S, the same day.
              TITANFALL_ACTION: TITANFALL_EVENTS}[action]
    if event not in events:
        raise DataError(
            f"Enchantment Effects row {index}: {who} acts as {action} on the "
            f"event {event or '(none)'!r}. It is ruled on {', '.join(events)} "
            f"only.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} acts as {action} and "
                f"states {column} {written!r}, which means nothing to it, so the "
                f"column must be empty.")


def _check_strike_target_action(index: int, who: str, action: str, event: str,
                                fraction_of: str, kind: str, raw,
                                headers: dict[str, int]) -> None:
    """Everything a strike row must say, and everything it must not. Ruled
    2026-10-07. The event must name a living character other than the wearer,
    because that character is who is struck; the size is checked where the
    value is read. A fraction, a value kind and a scale each mean nothing here,
    so each is refused rather than dropped.
    """
    if event not in STRIKE_TARGET_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} strikes the other "
            f"character of the event {event or '(none)'!r}. Only an event that "
            f"names a living character other than the wearer has one to "
            f"strike: {', '.join(STRIKE_TARGET_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} strikes the other "
                f"character of its event and states {column} {written!r}. Its "
                f"value is a percentage of attack damage and nothing else, so "
                f"the column must be empty.")


def _check_execute_target_action(index: int, who: str, action: str, event: str,
                                 fraction_of: str, kind: str, raw,
                                 headers: dict[str, int]) -> None:
    """Everything an execute row must say, and everything it must not. Ruled
    2026-10-12. ON `hit_dealt` ONLY, because that is the one event raised where
    the engine can act once the hit's damage is written. THE CONDITION MUST BE
    `target_health_below`, because its value is the share of health the row
    executes below and the engine reads no other from this action; a row with
    no condition would execute nobody and say so nowhere. The chance and the
    share are checked where the value is read. A fraction, a value kind and a
    scale each mean nothing here, so each is refused rather than dropped.
    """
    if event not in EXECUTE_TARGET_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} executes an enemy on the "
            f"event {event or '(none)'!r}. Only a hit dealt names the enemy "
            f"struck where its health is written: "
            f"{', '.join(EXECUTE_TARGET_EVENTS)}.")
    condition = clean(_cell(raw, headers, "Condition")).lower()
    if condition != EXECUTE_TARGET_CONDITION:
        raise DataError(
            f"Enchantment Effects row {index}: {who} executes an enemy and "
            f"states the Condition {condition or '(none)'!r}. The share of "
            f"health it executes below is the value of the Condition "
            f"{EXECUTE_TARGET_CONDITION!r}, and the row states that condition "
            f"and no other.")
    # ABOVE 0 AND UP TO 100. The condition's own bound lets 0 through, and an
    # execute below 0% of health is a row that never acts and says so nowhere.
    share_text = clean(_cell(raw, headers, "Condition Value"))
    share = number(share_text, "Condition Value", index) if share_text else 0.0
    if not 0 < share <= 100:
        raise DataError(
            f"Enchantment Effects row {index}: {who} executes an enemy below "
            f"{share:g} per cent of its maximum health. The share is its "
            f"Condition Value, above 0 and up to 100.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} executes an enemy and "
                f"states {column} {written!r}. Its value is a chance and its "
                f"Condition Value the share of health, and nothing else, so "
                f"the column must be empty.")


def _check_damage_immunity_action(index: int, who: str, action: str, event: str,
                                  fraction_of: str, kind: str, raw,
                                  headers: dict[str, int]) -> None:
    """Everything a no-damage window row must say, and everything it must not.
    Issue #1833 group E part 2. An event is required, because the window opens
    AT something; the seconds are checked where the value is read. A fraction, a
    value kind and a scale each mean nothing here, so each is refused rather
    than dropped.
    """
    known = action_events()
    if event not in known:
        raise DataError(
            f"Enchantment Effects row {index}: {who} opens a no-damage window on "
            f"the event {event or '(none)'!r}, which the game does not fire. "
            f"Known: {', '.join(sorted(known))}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} opens a no-damage window "
                f"and states {column} {written!r}. Its value is seconds and "
                f"nothing else, so the column must be empty.")


def _check_teleport_randomly_action(index: int, who: str, action: str, event: str,
                                    fraction_of: str, kind: str, raw,
                                    headers: dict[str, int]) -> None:
    """Everything a row that moves its wearer to a random point must say, and
    everything it must not. Ruled 2026-10-12. ON `hit_taken` ONLY, the one event
    for which the engine holds whether the blow was a tick. IT MUST STATE ITS
    TRIGGER COOLDOWN, because the sentence states none and the wait is a judged
    number: an empty cell would take the quarter second every hit-fired event
    takes, and a wearer under steady hits would be moved four times a second
    with nothing in the row saying so. The metres are checked where the value
    is read. A fraction, a value kind and a scale each mean nothing here, and
    `hit_taken` carries no tags for a scoped row to match, so each is refused
    rather than dropped.
    """
    if event not in TELEPORT_RANDOMLY_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} moves its wearer to a "
            f"random point on the event {event or '(none)'!r}. The game moves "
            f"it for a hit taken and for nothing else: "
            f"{', '.join(TELEPORT_RANDOMLY_EVENTS)}.")
    if not clean(_cell(raw, headers, "Trigger Cooldown")):
        raise DataError(
            f"Enchantment Effects row {index}: {who} moves its wearer to a "
            f"random point and states no Trigger Cooldown. Its sentence states "
            f"none, so the row must: an empty cell would take the "
            f"{DEFAULT_TRIGGER_COOLDOWN:g} seconds of a hit-fired event.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale"))),
                            ("Required Tags",
                             clean(_cell(raw, headers, "Required Tags")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} moves its wearer to a "
                f"random point and states {column} {written!r}. Its value is "
                f"metres and its event carries no tags, so the column must be "
                f"empty.")


def _check_gadgets_cast_now_action(index: int, who: str, action: str, event: str,
                                   fraction_of: str, kind: str, raw,
                                   headers: dict[str, int]) -> None:
    """Everything a row that fires its wearer's gadgets at its death must say,
    and everything it must not. Ruled 2026-10-19 (C1). ON `player_death` ONLY,
    the one event the engine acts on for it. Its value of 100 is checked where
    the value is read. A fraction, a value kind and a scale each mean nothing
    here, and `player_death` carries no tags for a scoped row to match, so each
    is refused rather than dropped. A Trigger Cooldown is refused by the general
    check, because the action takes none: one death raises one event.
    """
    if event not in GADGETS_CAST_NOW_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} fires its wearer's gadgets "
            f"on the event {event or '(none)'!r}. The game fires them at the "
            f"wearer's death and at nothing else: "
            f"{', '.join(GADGETS_CAST_NOW_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale"))),
                            ("Required Tags",
                             clean(_cell(raw, headers, "Required Tags")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} fires its wearer's "
                f"gadgets and states {column} {written!r}. It has no size and "
                f"its event carries no tags, so the column must be empty.")


def _check_ailment_rider_action(index: int, who: str, action: str, event: str,
                                fraction_of: str, kind: str, raw,
                                headers: dict[str, int]) -> None:
    """Everything a rider on an ailment must not say. Issue #1833, ruled
    2026-10-06. NO EVENT, because nothing fires a rider: the game reads it when
    the wearer applies the ailment. A fraction, a value kind, a scale, tags and
    a condition are each refused rather than dropped; the game reads none of
    them on a rider. The Ailment and the value are checked where they are read.
    """
    for column, written in (("Action Event", event),
                            ("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale"))),
                            ("Required Tags",
                             clean(_cell(raw, headers, "Required Tags"))),
                            ("Condition", clean(_cell(raw, headers, "Condition")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} hangs a number on an "
                f"ailment and states {column} {written!r}. A rider is read when "
                f"its ailment is applied and by nothing else, so the column "
                f"must be empty.")


def _check_cleanse_action(index: int, who: str, action: str, event: str,
                          fraction_of: str, kind: str, raw,
                          headers: dict[str, int]) -> None:
    """Everything a cleanse row must say, and everything it must not. Issue
    #1833. THE TIMED EVENT ONLY, because that is the one path on which the game
    reads the flag; its period is checked where Every Seconds is read, and its
    value where the value is read. A fraction, a value kind and a scale mean
    nothing to a cleanse, and the timed event carries no tags for a scoped row
    to match, so each is refused rather than dropped. A condition is judged on
    the timed path as on any other, so it is allowed.
    """
    if event != TIMED_EVENT:
        raise DataError(
            f"Enchantment Effects row {index}: {who} cleanses on the event "
            f"{event or '(none)'!r}. The game cleanses on {TIMED_EVENT} and on "
            f"nothing else, so the row would do nothing.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale"))),
                            ("Required Tags",
                             clean(_cell(raw, headers, "Required Tags")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} cleanses and states "
                f"{column} {written!r}. A cleanse has no size and its event no tags, so "
                f"the column must be empty.")


def _check_repeat_skill_action(index: int, who: str, action: str, event: str,
                               fraction_of: str, kind: str, raw,
                               headers: dict[str, int]) -> None:
    """Everything a repeat row must say, and everything it must not. Mechanism
    B2. Its event must be one that names a skill, because the row repeats THAT
    skill; the chance is checked where the value is read. A fraction, a value
    kind and a scale each mean nothing here, so each is refused rather than
    dropped.

    THE SAME FOR A ROW THAT TRIGGERS A DIFFERENT HELD SKILL, which needs the
    skill of the use to leave it out. The messages say "repeats a skill" for
    all three, and name the action.
    """
    if event not in REPEAT_SKILL_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} repeats a skill on the "
            f"event {event or '(none)'!r}, which names no skill to repeat. "
            f"Known: {', '.join(REPEAT_SKILL_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} repeats a skill and "
                f"states {column} {written!r}. Its value is a chance and nothing "
                f"else, so the column must be empty.")


def _check_kept_stacks_repeat_skill_action(index: int, who: str, action: str,
                                           event: str, fraction_of: str,
                                           kind: str, raw,
                                           headers: dict[str, int]) -> None:
    """Everything a row that gains kept stacks and spends them to cast a skill
    again must say, and everything it must not. Ruled 2026-10-12.

    ITS EVENT IS THE KIND OF USE THAT SPENDS THE STACKS, one of
    `KEPT_STACKS_REPEAT_SKILL_EVENTS`. The period, the cap and the share are
    checked where Every Seconds, Scale Max Steps and the value are read.

    REQUIRED TAGS AND A CONDITION ARE REFUSED. The stacks are gained on the
    clock, which asks the row with no tags and no other character, as it asks
    every timed row. The game refuses a row that requires tags when it is
    asked with none, so a row stating them would never gain a stack. A
    condition would be judged at the clock and again at the use, and no
    sentence asks for either. A fraction, a value kind and a scale each mean
    nothing here.
    """
    if event not in KEPT_STACKS_REPEAT_SKILL_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} spends kept stacks on the "
            f"event {event or '(none)'!r}. The Action Event of {action} is the "
            f"kind of use that spends them, and the game spends them on: "
            f"{', '.join(KEPT_STACKS_REPEAT_SKILL_EVENTS)}. The clock that "
            f"gains them is Every Seconds and not an event.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale"))),
                            ("Required Tags",
                             clean(_cell(raw, headers, "Required Tags"))),
                            ("Condition", clean(_cell(raw, headers, "Condition")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} gains kept stacks on "
                f"a clock and states {column} {written!r}. The clock asks the "
                f"row with no tags, and its value is a share of damage and "
                f"nothing else, so the column must be empty.")


def _check_kept_stacks_free_next_ability_action(index: int, who: str,
                                                action: str, event: str,
                                                fraction_of: str, kind: str,
                                                raw,
                                                headers: dict[str, int]) -> None:
    """Everything a row that gains kept stacks on an event and buys a free,
    empowered next ability with them must say, and must not. Ruled 2026-10-17.

    ITS EVENT IS WHAT GAINS A STACK, one of
    `KEPT_STACKS_FREE_NEXT_ABILITY_EVENTS`. The cap, the value and the name are
    checked where Scale Max Steps, the value and Ailment are read.

    A CONDITION AND REQUIRED TAGS ARE ALLOWED: the event is a hit, which names
    the enemy struck and carries the hit's tags, so both can be judged, and the
    shipped row's "a silenced enemy" is its condition. A fraction, a value kind
    and a scale each mean nothing here.
    """
    if event not in KEPT_STACKS_FREE_NEXT_ABILITY_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} gains kept stacks on the "
            f"event {event or '(none)'!r}. The Action Event of {action} is the "
            f"event that gains one, and the game gains them on: "
            f"{', '.join(KEPT_STACKS_FREE_NEXT_ABILITY_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} gains kept stacks "
                f"towards a free next ability and states {column} "
                f"{written!r}. Its value is how much more damage that ability "
                f"deals and nothing else, so the column must be empty.")


def _check_nth_action(index: int, who: str, action: str, event: str,
                      fraction_of: str, kind: str, raw,
                      headers: dict[str, int]) -> None:
    """Everything an every-Nth row must not say. Issue #1833, phase 2.

    THE ACTION NAMES WHAT IS COUNTED, so an event is refused rather than
    dropped; so are a fraction, a value kind and a scale. Every Nth itself is
    checked where the column is read.
    """
    for column, written in (("Action Event", event),
                            ("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} is the every-Nth "
                f"action {action} and states {column} {written!r}. The action "
                f"names what it counts and what the Nth does, so the column "
                f"must be empty.")


def _check_cooldown_reset_action(index: int, who: str, action: str,
                                 event: str, fraction_of: str, kind: str, raw,
                                 headers: dict[str, int]) -> None:
    """Everything a cooldown reset row must say, and everything it must not.

    Issue #1833, the cooldown reset action. AN EVENT IS REQUIRED, because a
    reset happens AT something; the chance is checked where the value is read.
    A fraction, a value kind and a scale each mean nothing here, so each is
    refused rather than dropped.
    """
    known = granting_events()
    if not event:
        raise DataError(
            f"Enchantment Effects row {index}: {who} is the cooldown reset "
            f"{action} and names no event to reset on. Known: "
            f"{', '.join(sorted(known))}.")
    if event not in known:
        raise DataError(
            f"Enchantment Effects row {index}: {who} resets cooldowns on the "
            f"event {event!r}, which the game does not record. Known: "
            f"{', '.join(sorted(known))}.")
    # A REDUCTION'S SECONDS MAY BE SIZED BY THE EVENT'S AMOUNT, Turn of the
    # Wheel's "Every 10 Fervour you spend" (the Death trees' slice C3b). A
    # reset's chance may not: a chance multiplied is not a chance.
    if action in COOLDOWN_REDUCE_ACTIONS:
        fraction_of = _sized_by_the_amount(index, who, event, fraction_of)
        # AND BY THE WHOLE HUNDREDS OF THE CLASS RESOURCE HELD, Relentless's
        # "for every 100 Fervour held" (the Death trees' slice C4d1), on any
        # event: it reads the character, not the event.
        if fraction_of == FRACTION_OF_HUNDREDS_HELD:
            fraction_of = ""
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} is the cooldown reset "
                f"{action} and states {column} {written!r}. Its value is a "
                f"chance and nothing else, so the column must be empty.")


def _check_next_use_action(index: int, who: str, action: str, event: str,
                           fraction_of: str, kind: str, raw,
                           headers: dict[str, int]) -> None:
    """Everything a next-use row must say, and everything it must not.

    Issue #1833, phase 2. AN EVENT IS REQUIRED, because a charge is granted AT
    something; the cap is checked where Scale Max Steps is read. A fraction,
    a value kind and a scale each mean nothing here, so each is refused rather
    than dropped.
    """
    known = granting_events()
    if not event:
        raise DataError(
            f"Enchantment Effects row {index}: {who} grants a {action} "
            f"charge and names no event to grant it on. Known: "
            f"{', '.join(sorted(known))}.")
    if event not in known:
        raise DataError(
            f"Enchantment Effects row {index}: {who} grants a {action} "
            f"charge on the event {event!r}, which the game does not "
            f"record. Known: {', '.join(sorted(known))}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} grants a {action} "
                f"charge and states {column} {written!r}. A charge is worth "
                f"its value as increased damage and nothing else, so the "
                f"column must be empty.")


def _condition_and_scale(raw, headers: dict[str, int], sheet: str, index: int,
                         who: str) -> tuple[str, float, str, float]:
    """A row's condition, its value, its scale and its step, checked.

    THE ONE PLACE THESE RULES LIVE, for the Passive Effects sheet and the
    enchantment sheets alike. A condition the game cannot judge would be
    applied with no condition, and a scale it cannot judge would be worth
    nothing, so both are refused here and the error names what the game does
    know. `passive_effects` carried its own copy of these checks until issue
    #1593; `sheet` and `who` are what let one copy name either sheet's row.
    """
    condition = clean(_cell(raw, headers, "Condition")).lower()
    if condition and condition not in CONDITIONS:
        raise DataError(
            f"{sheet} row {index}: {who} names the condition {condition!r}, "
            f"which the game cannot judge. Known: "
            f"{', '.join(sorted(CONDITIONS))}.")

    condition_value = 0.0
    if condition and CONDITIONS[condition] is None:
        written = clean(_cell(raw, headers, "Condition Value"))
        if written:
            raise DataError(
                f"{sheet} row {index}: {who} carries the condition "
                f"{condition!r} and a condition value of {written!r}. That "
                f"condition compares nothing, so the value would be ignored. "
                f"Leave the column empty.")
    elif condition:
        condition_value = number(_cell(raw, headers, "Condition Value"),
                                 "Condition Value", index)
        low, high, units = CONDITIONS[condition]
        if not low <= condition_value <= high:
            raise DataError(
                f"{sheet} row {index}: {who} has a condition value of "
                f"{condition_value}, and {condition!r} takes {units} between "
                f"{low:g} and {high:g}.")
        # A FERVOUR THRESHOLD IS ONE OF FOUR FIGURES, not any number between
        # them: the engine reaches none for another, so the row would be dead.
        if (condition == "fervour_threshold_reached"
                and condition_value not in FERVOUR_THRESHOLDS):
            raise DataError(
                f"{sheet} row {index}: {who} asks for the Fervour threshold "
                f"{condition_value:g}, and the four are "
                f"{', '.join(f'{t:g}' for t in FERVOUR_THRESHOLDS)}.")

    scale = clean(_cell(raw, headers, "Scale")).lower()
    if scale and scale not in SCALES:
        raise DataError(
            f"{sheet} row {index}: {who} names the scale {scale!r}, which the "
            f"game cannot judge. Known: {', '.join(sorted(SCALES))}.")

    scale_step = 0.0
    if scale:
        scale_step = number(_cell(raw, headers, "Scale Step"), "Scale Step",
                            index)
        low, high, units = SCALES[scale]
        if scale_step <= 0.0 or not low <= scale_step <= high:
            raise DataError(
                f"{sheet} row {index}: {who} has a scaling step of "
                f"{scale_step}, and {scale!r} takes {units} above 0 and up to "
                f"{high:g}. A step of nothing is worth nothing at every state.")

    return condition, condition_value, scale, scale_step


def _effect_row(raw, headers: dict[str, int], index: int, name: str,
                words: dict[str, str], types: dict[str, str],
                rolls_down: dict[str, frozenset[int]], *,
                sheet_repeats_the_words: bool = True) -> dict:
    """One row in the Enchantment Effects sheet's shape, checked, as a dict
    without its row name.

    THE PER-ROW RULES OF `enchantment_effects`, MOVED HERE UNCHANGED, so that
    `passive_effects` reads a passive row carrying the action columns by the
    same rules rather than by a copy of them that could drift. The passive row
    port of 2026-11-24. `words` holds each name's sentence, `types` its
    enchantment type ("Set" or not) and `rolls_down` the places of its ranges
    that roll downward, as `enchantment_effects` gathers them; the passive
    reader hands over the node's own sentence, no type and no ranges.

    `sheet_repeats_the_words` IS FALSE FOR THE PASSIVE SHEET, which has no
    `Effect` column repeating a sentence: a node's words are in the tree files,
    and the passive reader hands them over in `words` for the rules that read
    them.
    """
    if name not in words:
        raise DataError(
            f"Enchantment Effects row {index}: no enchantment is called "
            f"{name!r}. Use the row name game/Data/EnchantmentsPositive.csv "
            f"or game/Data/EnchantmentsNegative.csv gives it.")

    said = (clean(_cell(raw, headers, "Effect")) if sheet_repeats_the_words
            else words[name])
    if said != words[name]:
        raise DataError(
            f"Enchantment Effects row {index}: {name} is written here as "
            f"{said!r} and in the Enchantments sheet as {words[name]!r}. "
            f"The two must agree, so that an enchantment that is reworded "
            f"is read again before its numbers are trusted.")

    stat = clean(_cell(raw, headers, "Stat"))
    action = clean(_cell(raw, headers, "Action")).lower()
    action_event = clean(_cell(raw, headers, "Action Event")).lower()
    fraction_of = clean(_cell(raw, headers, "Fraction Of")).lower()
    kind = clean(_cell(raw, headers, "Value Kind")).lower()

    # A ROW CHANGES A STAT OR MOVES A POOL, NEVER BOTH AND NEVER NEITHER.
    # Both would be two effects wearing one roll, and the hover text shows
    # one number; neither is a row that validates and grants nothing.
    if stat and action:
        raise DataError(
            f"Enchantment Effects row {index}: {name} names both the stat "
            f"{stat!r} and the pool action {action!r}. A row does one or "
            f"the other: a stat row changes a number the pipeline reads, "
            f"an action row moves a pool when an event happens.")
    if not stat and not action:
        raise DataError(
            f"Enchantment Effects row {index}: {name} names no stat and no "
            f"pool action. Known pools: {', '.join(POOL_ACTIONS)}.")

    if action:
        _check_pool_action(index, name, action, action_event, fraction_of,
                           kind, raw, headers)
        if action not in NEXT_USE_ACTIONS and action not in PLACED_ACTIONS \
                and action not in NTH_ACTIONS \
                and action not in COOLDOWN_RESET_ACTIONS \
                and action not in COOLDOWN_REDUCE_ACTIONS \
                and action != RANDOM_DOT_ACTION \
                and action != HEALTH_CAP_ACTION \
                and action not in NEARBY_ACTIONS \
                and action not in REMAINING_DAMAGE_ACTIONS \
                and action not in ALL_APPLY_STATUS_ACTIONS \
                and action != DAMAGE_IMMUNITY_ACTION \
                and action != TELEPORT_RANDOMLY_ACTION \
                and action != GADGETS_CAST_NOW_ACTION \
                and action != REFLECT_BLOCKED_ACTION \
                and action != BLAST_FROM_THE_DYING_ACTION \
                and action != STUN_NEAR_THE_DYING_ACTION \
                and action != SLOW_FOR_SECONDS_ACTION \
                and action != CLEANSE_ACTION \
                and action != OVERKILL_EXPLOSION_ACTION \
                and action != KILLING_BLOW_EXPLOSION_ACTION \
                and action != SLAIN_BURST_ACTION \
                and action != BLEED_LEFT_TO_STRONGEST_ACTION \
                and action != NEXT_HIT_CRITICAL_ACTION \
                and action != FERVOUR_HOLD_EXTENDED_ACTION \
                and action != NEXT_HIT_CRITICAL_TWICE_ACTION \
                and action not in STRONGEST_NEAR_ACTIONS \
                and action not in TRAPS_NEARBY_ACTIONS \
                and action != GADGET_FIRES_AT_TRAP_ENEMY_ACTION \
                and action != STRIKE_TARGET_ACTION \
                and action != EXECUTE_TARGET_ACTION \
                and action != TEMPORARY_ABSORB_ACTION \
                and action != RETALIATION_PULSE_ACTION \
                and action != FRENZY_CHAIN_ACTION \
                and action != TAUNT_NEAR_TRAP_ACTION \
                and action != PLACE_LAST_TRAP_AGAIN_ACTION \
                and action != COUNTERFIRE_ACTION \
                and action != COPY_LAST_TRAP_AT_KILL_ACTION \
                and action != CROSSFIRE_ACTION \
                and action != SCRAP_MINE_ACTION \
                and action != TITANFALL_ACTION \
                and action != KEPT_STACKS_REPEAT_SKILL_ACTION \
                and action != KEPT_STACKS_FREE_NEXT_ABILITY_ACTION \
                and action not in SKILL_IN_HAND_ACTIONS \
                and action not in AILMENT_RIDER_ACTIONS:
            fraction_of = fraction_of or FRACTION_BASES[0]
    else:
        _check_value_kind("Enchantment Effects", index, name, stat, kind)

    low = number(_cell(raw, headers, "Value Low"), "Value Low", index)
    high_text = clean(_cell(raw, headers, "Value High"))
    high = number(high_text, "Value High", index) if high_text else low

    # CHECKED BEFORE THE RANGE BELOW, so that a range on a set row is
    # reported for what it is rather than as a range its words do not
    # state.
    if types[name].casefold() == "set" and high != low:
        raise DataError(
            f"Enchantment Effects row {index}: {name} is a set row stating "
            f"the range {low:g} to {high:g}. Each of a set's rows states "
            f"one number: an item records only the set's lowest threshold "
            f"row, so nothing records a roll for the 6-piece or 10-piece "
            f"row and a range could not be read back.")

    value_as_stated = None
    if high != low:
        # A RANGE IS ONE THE SENTENCE STATES, IN ITS ORDER AND WITH ONE
        # SIGN. The owner ruled on 2026-09-11 that an enchantment rolls a
        # value evenly inside its range, and the game shows that number in
        # place of the range in the sentence, so a row whose two ends are
        # not that range would give the character one number and the
        # player another. "Low" is the value at the lowest roll and "high"
        # at the highest, so "reduced by 30%-50%" is -30 and -50.
        stated = enchantment_ranges(words[name])
        # OR ITS COMPLEMENT, FOR THE FEW NAMED. See
        # `COMPLEMENT_RANGE_ENCHANTMENTS`: both ends negative, each 100
        # minus the sentence's number at the same place.
        shown = ((100.0 - abs(low), 100.0 - abs(high))
                 if name in COMPLEMENT_RANGE_ENCHANTMENTS and low < 0
                 else (abs(low), abs(high)))
        # OR ITS BASE PLUS THE VALUE, FOR THE FEW NAMED. See
        # `BASE_PLUS_RANGE_ENCHANTMENTS`: each end the base plus the value.
        if name in BASE_PLUS_RANGE_ENCHANTMENTS and low >= 0:
            base = BASE_PLUS_RANGE_ENCHANTMENTS[name]
            shown = (base + low, base + high)
        if (low < 0) != (high < 0) or shown not in stated:
            written = ", ".join(f"{a:g} to {b:g}" for a, b in stated)
            raise DataError(
                f"Enchantment Effects row {index}: {name} states the "
                f"range {low:g} to {high:g}, which its words "
                f"{words[name]!r} do not state in that order. The ranges "
                f"they state: {written or 'none'}. Write the first number "
                f"as Value Low and the second as Value High, both with "
                f"the row's sign.")
        value_as_stated = shown

    condition, condition_value, scale, scale_step = _condition_and_scale(
        raw, headers, "Enchantment Effects", index, name)

    # A CONDITION READ FROM AN ACTION'S EVENT NEEDS AN ACTION AND AN EVENT
    # CARRYING THE HIT'S TAGS. Ruled 2026-10-17. On a stat row it is
    # refused by `refuse_a_condition_nothing_asks_for`; here, an action row
    # on any other event would judge an event with no hit and hold every
    # time, which is "not a spell" said of nothing.
    if condition in CONDITIONS_OF_AN_ACTIONS_EVENT and action \
            and action_event not in EVENTS_CARRYING_A_HIT_DEALTS_TAGS:
        raise DataError(
            f"Enchantment Effects row {index}: {name} moves {action} on the "
            f"event {action_event or '(none)'!r} under the condition "
            f"{condition!r}, which is judged from the tags of a hit the "
            f"wearer dealt. Only these events carry them: "
            f"{', '.join(EVENTS_CARRYING_A_HIT_DEALTS_TAGS)}.")

    # "UP TO 10 STACKS" IS A CAP OF 10 WHOLE STEPS. Issue #1815. This sheet
    # carried the column alone until 2026-11-24, when `passive_effects`
    # began reading it by these same rules for the demonic tree rework.
    # Empty is no cap. A cap needs a scale to cap, and is a whole number of
    # steps, from 1 up to the largest a sentence states.
    cap_text = clean(_cell(raw, headers, "Scale Max Steps"))
    scale_max_steps = 0
    if cap_text and scale in SCALES_THAT_COUNT_A_FRACTION:
        raise DataError(
            f"Enchantment Effects row {index}: {name} scales by {scale!r} "
            f"and states Scale Max Steps {cap_text!r}. That scale counts a "
            f"fraction of a charge's range and not whole steps, and is "
            f"capped at the row's whole value by itself, so there is "
            f"nothing for a cap in steps to limit. Leave the column empty.")
    if cap_text:
        cap = number(cap_text, "Scale Max Steps", index)
        if not scale and action not in NEXT_USE_ACTIONS \
                and action not in PLACED_ACTIONS \
                and action != KEPT_STACKS_REPEAT_SKILL_ACTION \
                and action != KEPT_STACKS_FREE_NEXT_ABILITY_ACTION \
                and action not in TRAPS_NEARBY_ACTIONS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} has a cap of "
                f"{cap:g} steps and no scale. A cap limits how many steps "
                f"a scale counts, so with no scale it caps nothing.")
        if cap != int(cap) or not 1 <= cap <= MAX_SCALE_STEPS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} has a cap of "
                f"{cap:g} steps. A cap is a whole number of steps from 1 "
                f"to {MAX_SCALE_STEPS}; leave the column empty for none.")
        scale_max_steps = int(cap)
    if scale in SCALES_THAT_NEED_A_CAP and scale_max_steps < 1:
        raise DataError(
            f"Enchantment Effects row {index}: {name} scales by {scale!r} "
            f"and states no Scale Max Steps. That scale has no cap of its "
            f"own, so the row would be uncapped. State the cap.")

    # A NEXT-USE ROW STATES HOW MANY CHARGES IT HOLDS. Issue #1833, phase
    # 2: one for a sentence stating no stacking, five for "stacking up to
    # 5 times". Required rather than defaulted, so the one judgement a row
    # makes is written in the sheet.
    if action in NEXT_USE_ACTIONS:
        if scale_max_steps < 1:
            raise DataError(
                f"Enchantment Effects row {index}: {name} grants a "
                f"{action} charge and states no Scale Max Steps. It is "
                f"how many charges the row holds: 1 when the sentence "
                f"states no stacking.")
        if low <= 0 or high <= 0:
            raise DataError(
                f"Enchantment Effects row {index}: {name} grants a "
                f"{action} charge worth {low:g} to {high:g}. A charge is "
                f"increased damage, so both ends are above nought.")

    # A ROW THAT GAINS KEPT STACKS STATES THE MOST IT HOLDS, AND ITS VALUE
    # IS THE SHARE OF ITS DAMAGE EACH EXTRA CAST DEALS, above 0 and up to
    # 100. Ruled 2026-10-12. The cap is required rather than defaulted: "to
    # at most 5" is the ruling's and not the sentence's, so the sheet is
    # where it is written.
    if action == KEPT_STACKS_REPEAT_SKILL_ACTION:
        if scale_max_steps < 1:
            raise DataError(
                f"Enchantment Effects row {index}: {name} gains kept "
                f"stacks and states no Scale Max Steps. On {action} that "
                f"column is the most stacks held, a whole number from 1.")
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} casts a skill "
                f"again at {low:g} to {high:g} per cent of its damage. On "
                f"{action} the value is that share, above 0 and up to "
                f"100, and 100 is the whole.")

    # AND A ROW THAT GAINS THEM TOWARDS A FREE NEXT ABILITY STATES THE CAP
    # THE ABILITY IS BOUGHT AT, AND ITS VALUE IS HOW MUCH MORE DAMAGE THAT
    # ABILITY DEALS, in per cent, above 0. Ruled 2026-10-17: "At 10 stacks"
    # is the cap and "100% more" the value.
    if action == KEPT_STACKS_FREE_NEXT_ABILITY_ACTION:
        if scale_max_steps < 1:
            raise DataError(
                f"Enchantment Effects row {index}: {name} gains kept "
                f"stacks and states no Scale Max Steps. On {action} that "
                f"column is the count the next ability is bought at, a "
                f"whole number from 1.")
        if not (0 < low <= MAX_FREE_NEXT_ABILITY_MORE_PERCENT
                and 0 < high <= MAX_FREE_NEXT_ABILITY_MORE_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} makes its free "
                f"next ability deal {low:g} to {high:g} per cent more "
                f"damage. On {action} the value is that more, above 0 and "
                f"up to {MAX_FREE_NEXT_ABILITY_MORE_PERCENT:g}: 100 is "
                f"twice.")

    # A COOLDOWN RESET'S VALUE IS ITS CHANCE, above 0 and up to 100. Issue
    # #1833, the cooldown reset action. 100 is always.
    if action in COOLDOWN_RESET_ACTIONS:
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} resets cooldowns "
                f"with a chance of {low:g} to {high:g}. A chance is above 0 "
                f"and up to 100, and 100 is always.")
        if scale_max_steps:
            raise DataError(
                f"Enchantment Effects row {index}: {name} resets cooldowns "
                f"and states Scale Max Steps. A reset has no stacks, so it "
                f"would be dropped.")

    # A RANDOM DAMAGE OVER TIME'S VALUE IS ITS CHANCE, above 0 and up to
    # 100, as a reset's is. Issue #1833 group D. 100 is always.
    if action == RANDOM_DOT_ACTION:
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} applies a random "
                f"damage over time with a chance of {low:g} to {high:g}. A "
                f"chance is above 0 and up to 100, and 100 is always.")
        if scale_max_steps:
            raise DataError(
                f"Enchantment Effects row {index}: {name} applies a random "
                f"damage over time and states Scale Max Steps. It has no "
                f"stacks, so it would be dropped.")

    # A HEALTH CAP'S VALUE IS A SHARE OF MAXIMUM HEALTH, above 0 and below
    # 100, since 100 would cap nothing. Issue #1833 group D part 2.
    if action == HEALTH_CAP_ACTION:
        if not (0 < low < 100 and 0 < high < 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} lowers health to "
                f"{low:g} to {high:g} per cent of its maximum. The share is "
                f"above 0 and below 100.")
        if scale_max_steps:
            raise DataError(
                f"Enchantment Effects row {index}: {name} lowers health and "
                f"states Scale Max Steps. It has no stacks, so it would be "
                f"dropped.")

    # A NEARBY ACTION'S VALUE IS A SHARE, above 0 and up to its bound: of
    # weapon damage for a smite, of each one's maximum health for a heal.
    # Issue #1833 group D part 3.
    if action in NEARBY_ACTIONS:
        bound = MAX_NEARBY_PERCENT[action]
        if not (0 < low <= bound and 0 < high <= bound):
            raise DataError(
                f"Enchantment Effects row {index}: {name} acts on the "
                f"characters nearby at {low:g} to {high:g} per cent. "
                f"{action} takes above 0 and up to {bound:g}.")

    # A REFLECT'S VALUE IS A SHARE OF WHAT THE BLOCK REMOVED, above 0 and up
    # to 100. Issue #1833 group E part 3.
    if action == REFLECT_BLOCKED_ACTION:
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} reflects {low:g} to "
                f"{high:g} per cent of what a block removed. It reflects above "
                f"0 and up to 100.")

    # A BLAST'S VALUE IS A SHARE OF THE DEAD ENEMY'S MAXIMUM HEALTH, above 0
    # and up to 100. Ruled 2026-10-07.
    if action == BLAST_FROM_THE_DYING_ACTION:
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} blasts for {low:g} "
                f"to {high:g} per cent of a dead enemy's maximum health. It "
                f"blasts for above 0 and up to 100.")

    # A STUN NEAR THE DYING'S VALUE IS ITS CHANCE, above 0 and up to 100,
    # rolled once for the death. Ruled 2026-10-08. 100 is always. Its
    # seconds are checked where Stack Seconds is read.
    if action == STUN_NEAR_THE_DYING_ACTION:
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} stuns the enemies "
                f"near an enemy that died with a chance of {low:g} to "
                f"{high:g}. A chance is above 0 and up to 100, and 100 is "
                f"always.")

    # AN OVERKILL EXPLOSION'S VALUE IS A SHARE OF THE OVERKILL, above 0 and
    # up to the bound. Ruled 2026-10-07.
    if action == OVERKILL_EXPLOSION_ACTION:
        if not (0 < low <= MAX_OVERKILL_EXPLOSION_SHARE
                and 0 < high <= MAX_OVERKILL_EXPLOSION_SHARE):
            raise DataError(
                f"Enchantment Effects row {index}: {name} explodes a killed "
                f"enemy for {low:g} to {high:g} per cent of its overkill. "
                f"The share is above 0 and up to "
                f"{MAX_OVERKILL_EXPLOSION_SHARE:g}.")
    # AND A KILLING BLOW EXPLOSION'S IS A SHARE OF THAT BLOW, the same way.
    # Ruled 2026-10-21 (traps layer 4, F10).
    if action == KILLING_BLOW_EXPLOSION_ACTION:
        if not (0 < low <= MAX_KILLING_BLOW_EXPLOSION_SHARE
                and 0 < high <= MAX_KILLING_BLOW_EXPLOSION_SHARE):
            raise DataError(
                f"Enchantment Effects row {index}: {name} explodes a killed "
                f"enemy for {low:g} to {high:g} per cent of the blow that "
                f"killed it. The share is above 0 and up to "
                f"{MAX_KILLING_BLOW_EXPLOSION_SHARE:g}.")

    # AND A SLAIN CREATURE'S BURST IS A SHARE OF ITS MAXIMUM HEALTH, the same
    # way. The Death trees' slice C4c1 (2026-11-25).
    if action == SLAIN_BURST_ACTION:
        if not (0 < low <= MAX_SLAIN_BURST_SHARE and 0 < high <= MAX_SLAIN_BURST_SHARE):
            raise DataError(
                f"Enchantment Effects row {index}: {name} bursts a killed enemy "
                f"for {low:g} to {high:g} per cent of its maximum health. The "
                f"share is above 0 and up to {MAX_SLAIN_BURST_SHARE:g}.")

    # AND BLOOD DEBT'S IS THE SECONDS THE BLEED LEFT IS DEALT OVER. The Death
    # trees' slice C4c2 (2026-11-25).
    if action == BLEED_LEFT_TO_STRONGEST_ACTION:
        if not (0 < low <= MAX_BLEED_LEFT_SECONDS and 0 < high <= MAX_BLEED_LEFT_SECONDS):
            raise DataError(
                f"Enchantment Effects row {index}: {name} passes a Bleed's "
                f"damage left over {low:g} to {high:g} seconds. The seconds are "
                f"above 0 and up to {MAX_BLEED_LEFT_SECONDS:g}.")

    # AND CHAIN EXECUTION'S IS THE SECONDS ITS CHARGE LASTS. The Death trees'
    # slice C4d1 (2026-11-25).
    if action == NEXT_HIT_CRITICAL_ACTION:
        if not (0 <= low <= MAX_NEXT_HIT_CRITICAL_SECONDS and 0 <= high <= MAX_NEXT_HIT_CRITICAL_SECONDS):
            raise DataError(
                f"Enchantment Effects row {index}: {name} makes the next hit a "
                f"critical strike for {low:g} to {high:g} seconds. The seconds "
                f"are 0 (until used) up to {MAX_NEXT_HIT_CRITICAL_SECONDS:g}.")

    # AND BLADE ECHO'S AND THOUSAND CUTS' IS THE PER CENT OF WEAPON DAMAGE THEIR
    # HIT DEALS. The Death trees' slice C6e.
    if action in STRONGEST_NEAR_ACTIONS:
        if not (0 < low <= MAX_STRIKE_TARGET_PERCENT and 0 < high <= MAX_STRIKE_TARGET_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} strikes the strongest "
                f"enemy near for {low:g} to {high:g}% of weapon damage. The per "
                f"cent is above 0 and up to {MAX_STRIKE_TARGET_PERCENT:g}.")

    # AND TWICE ANSWERED'S IS THE CRITICAL STRIKES THE CHARGE HOLDS, two and
    # nothing else; its chance is the Chance Percent. The Death trees' slice C6b4.
    if action == NEXT_HIT_CRITICAL_TWICE_ACTION:
        if not (low == NEXT_HIT_CRITICAL_TWICE_COUNT and high == NEXT_HIT_CRITICAL_TWICE_COUNT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} doubles an evasion's "
                f"critical strike charge to {low:g} to {high:g}. It holds "
                f"{NEXT_HIT_CRITICAL_TWICE_COUNT:g} and no other number.")

    # AND KEEP THE COUNT'S IS THE SECONDS IT ADDS. The Death trees' slice C6a.
    if action == FERVOUR_HOLD_EXTENDED_ACTION:
        if not (0 < low <= MAX_FERVOUR_HOLD_EXTENDED_SECONDS and 0 < high <= MAX_FERVOUR_HOLD_EXTENDED_SECONDS):
            raise DataError(
                f"Enchantment Effects row {index}: {name} lengthens the hold "
                f"before decay by {low:g} to {high:g} seconds. The seconds are "
                f"above 0 and up to {MAX_FERVOUR_HOLD_EXTENDED_SECONDS:g}.")

    # AN ACTION ON THE WEARER'S OTHER TRAPS STATES A RADIUS IN METRES, above
    # 0 and up to the bound, OR NOUGHT AT THE WEARER'S DEATH, every trap; and
    # its Scale Max Steps is 1, the nearest trap alone, or empty, every trap.
    # Ruled 2026-10-22 (traps layer 5, H10, H11).
    if action in TRAPS_NEARBY_ACTIONS:
        if action_event in TRAPS_NEARBY_EVERY_TRAP_EVENTS:
            if not (low == 0 and high == 0):
                raise DataError(
                    f"Enchantment Effects row {index}: {name} acts on the "
                    f"wearer's traps on {action_event!r} within {low:g} to "
                    f"{high:g} metres. That event names no place to measure "
                    f"from, so the radius is 0, every trap.")
            if scale_max_steps:
                raise DataError(
                    f"Enchantment Effects row {index}: {name} acts on the "
                    f"wearer's traps on {action_event!r} and states Scale "
                    f"Max Steps {scale_max_steps}. That event acts on every "
                    f"trap, so the column must be empty.")
        elif not (0 < low <= MAX_TRAPS_NEARBY_METRES
                  and 0 < high <= MAX_TRAPS_NEARBY_METRES):
            raise DataError(
                f"Enchantment Effects row {index}: {name} acts on the "
                f"wearer's traps within {low:g} to {high:g} metres. The "
                f"radius is above 0 and up to "
                f"{MAX_TRAPS_NEARBY_METRES:g}.")
        if scale_max_steps not in (0, 1):
            raise DataError(
                f"Enchantment Effects row {index}: {name} acts on the "
                f"wearer's traps and states Scale Max Steps "
                f"{scale_max_steps}. It is 1, the nearest trap alone, or "
                f"empty, every trap.")

    # A GADGET'S SHOT AT A TRAP'S ENEMY STATES ITS "MORE" IN PER CENT, above
    # 0 and up to the bound: 100 is the gadget's normal blow. Ruled
    # 2026-10-22 (traps layer 6, J6, J10).
    if action == GADGET_FIRES_AT_TRAP_ENEMY_ACTION:
        if not (0 < low <= MAX_GADGET_SHOT_AT_TRAP_ENEMY_PERCENT
                and 0 < high <= MAX_GADGET_SHOT_AT_TRAP_ENEMY_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} fires a gadget at "
                f"a trap's enemy at {low:g} to {high:g} per cent of its "
                f"normal blow. The figure is above 0 and up to "
                f"{MAX_GADGET_SHOT_AT_TRAP_ENEMY_PERCENT:g}; 100 is its "
                f"normal blow.")

    # A STRIKE'S VALUE IS A PERCENTAGE OF THE WEARER'S ATTACK DAMAGE, above
    # 0 and up to the bound. Ruled 2026-10-07.
    if action == STRIKE_TARGET_ACTION:
        if not (0 < low <= MAX_STRIKE_TARGET_PERCENT
                and 0 < high <= MAX_STRIKE_TARGET_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} strikes for "
                f"{low:g} to {high:g} per cent of attack damage. A strike "
                f"is above 0 and up to {MAX_STRIKE_TARGET_PERCENT:g}.")

    # AN EXECUTE'S VALUE IS ITS CHANCE, above 0 and up to 100. Ruled
    # 2026-10-12. 100 is always. Its share of health is checked where the
    # Condition Value is read.
    if action == EXECUTE_TARGET_ACTION:
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} executes an enemy "
                f"with a chance of {low:g} to {high:g}. A chance is above 0 "
                f"and up to 100, and 100 is always.")

    # A TEMPORARY ABSORB'S VALUE IS A PERCENTAGE OF THE WEARER'S MAXIMUM
    # HEALTH, above 0 and up to the bound. Ruled 2026-10-07.
    if action == TEMPORARY_ABSORB_ACTION:
        if not (0 < low <= MAX_TEMPORARY_ABSORB_PERCENT
                and 0 < high <= MAX_TEMPORARY_ABSORB_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} grants a temporary "
                f"absorb of {low:g} to {high:g} per cent of maximum health. "
                f"It is above 0 and up to {MAX_TEMPORARY_ABSORB_PERCENT:g}.")

    # A RETALIATION PULSE'S VALUE IS A SHARE OF THE RETALIATION THE BIG HIT
    # EARNS, above 0 and up to the bound. V3a, 2026-11-25.
    if action == RETALIATION_PULSE_ACTION:
        if not (0 < low <= MAX_RETALIATION_PULSE_PERCENT
                and 0 < high <= MAX_RETALIATION_PULSE_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} pays a retaliation "
                f"pulse of {low:g} to {high:g} per cent. It is above 0 and up "
                f"to {MAX_RETALIATION_PULSE_PERCENT:g}.")

    # BAITED'S VALUE IS THE TAUNT'S SECONDS, above 0 and up to the bound; TRAIL
    # OF WIRE'S IS 100, always. V4a, 2026-11-25.
    if action == TAUNT_NEAR_TRAP_ACTION:
        if not (0 < low <= MAX_TAUNT_NEAR_TRAP_SECONDS
                and 0 < high <= MAX_TAUNT_NEAR_TRAP_SECONDS):
            raise DataError(
                f"Enchantment Effects row {index}: {name} taunts for {low:g} to "
                f"{high:g} seconds. It is above 0 and up to "
                f"{MAX_TAUNT_NEAR_TRAP_SECONDS:g}.")
    # WHERE THEY FALL'S VALUE IS THE COPY'S SECONDS, AND CROSSFIRE'S A SHARE OF
    # THE HIT, each above 0 and up to its bound. V5a, 2026-11-25.
    # TITANFALL'S VALUE IS ITS SECONDS UNKILLABLE, above 0 and up to the bound.
    # 2026-11-25.
    if action == TITANFALL_ACTION:
        if not (0 < low <= MAX_TITANFALL_SECONDS
                and 0 < high <= MAX_TITANFALL_SECONDS):
            raise DataError(
                f"Enchantment Effects row {index}: {name} leaves its wearer "
                f"unkillable for {low:g} to {high:g} seconds. It is above 0 and "
                f"up to {MAX_TITANFALL_SECONDS:g}.")
    # A SCRAP TRAP'S VALUE IS ITS SHOT'S PER CENT. V5b1, 2026-11-25.
    if action == SCRAP_MINE_ACTION:
        if not (0 < low <= MAX_SCRAP_MINE_PERCENT
                and 0 < high <= MAX_SCRAP_MINE_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} leaves a scrap trap "
                f"firing at {low:g} to {high:g} per cent. It is above 0 and up "
                f"to {MAX_SCRAP_MINE_PERCENT:g}.")
    if action in (COPY_LAST_TRAP_AT_KILL_ACTION, CROSSFIRE_ACTION):
        bound = (MAX_COPY_LAST_TRAP_AT_KILL_SECONDS
                 if action == COPY_LAST_TRAP_AT_KILL_ACTION
                 else MAX_CROSSFIRE_PERCENT)
        if not (0 < low <= bound and 0 < high <= bound):
            raise DataError(
                f"Enchantment Effects row {index}: {name} acts as {action} with "
                f"the value {low:g} to {high:g}. It is above 0 and up to "
                f"{bound:g}.")

    # COUNTERFIRE'S VALUE IS A SHARE OF A GADGET'S SHOT, above 0 and up to the
    # bound. V4b, 2026-11-25.
    if action == COUNTERFIRE_ACTION:
        if not (0 < low <= MAX_COUNTERFIRE_PERCENT
                and 0 < high <= MAX_COUNTERFIRE_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} answers with {low:g} "
                f"to {high:g} per cent of a shot. It is above 0 and up to "
                f"{MAX_COUNTERFIRE_PERCENT:g}.")
    if action == PLACE_LAST_TRAP_AGAIN_ACTION:
        if not (low == 100 and high == 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} places the last trap "
                f"again with the value {low:g} to {high:g}. It is 100, always: "
                f"the sentence states no chance.")

    # A FRENZY CHAIN'S VALUE IS A SHARE OF WHAT THE ATTACK SENT, above 0 and up
    # to the bound. V3b, 2026-11-25.
    if action == FRENZY_CHAIN_ACTION:
        if not (0 < low <= MAX_FRENZY_CHAIN_PERCENT
                and 0 < high <= MAX_FRENZY_CHAIN_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} chains an attack for "
                f"{low:g} to {high:g} per cent. It is above 0 and up to "
                f"{MAX_FRENZY_CHAIN_PERCENT:g}.")

    # A REPEAT'S VALUE IS A CHANCE, above 0 and up to 100. Mechanism B2.
    if action in SKILL_IN_HAND_ACTIONS:
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} repeats a skill "
                f"with a chance of {low:g} to {high:g}. A chance is above 0 "
                f"and up to 100, and 100 is always.")

    # A NO-DAMAGE WINDOW'S VALUE IS SECONDS, above 0 and up to the bound.
    # Issue #1833 group E part 2.
    if action == DAMAGE_IMMUNITY_ACTION:
        if not (0 < low <= MAX_DAMAGE_IMMUNITY_SECONDS
                and 0 < high <= MAX_DAMAGE_IMMUNITY_SECONDS):
            raise DataError(
                f"Enchantment Effects row {index}: {name} opens a no-damage "
                f"window of {low:g} to {high:g} seconds. It lasts above 0 and "
                f"up to {MAX_DAMAGE_IMMUNITY_SECONDS:g}.")

    # A MOVE TO A RANDOM POINT STATES METRES, above 0 and up to the bound.
    # Ruled 2026-10-12.
    if action == TELEPORT_RANDOMLY_ACTION:
        if not (0 < low <= MAX_TELEPORT_RANDOMLY_METRES
                and 0 < high <= MAX_TELEPORT_RANDOMLY_METRES):
            raise DataError(
                f"Enchantment Effects row {index}: {name} moves its wearer "
                f"to a random point within {low:g} to {high:g} metres. It is "
                f"above 0 and up to {MAX_TELEPORT_RANDOMLY_METRES:g}.")

    # A RIDER'S VALUE IS A PERCENT, above 0 and up to 100. Issue #1833.
    if action in AILMENT_RIDER_ACTIONS:
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} hangs {low:g} to "
                f"{high:g} per cent on an ailment. A rider moves a number by "
                f"above 0 and up to 100 per cent.")
        if scale_max_steps:
            raise DataError(
                f"Enchantment Effects row {index}: {name} hangs a number on "
                f"an ailment and states Scale Max Steps. It has no stacks, "
                f"so it would be dropped.")
        # AND THE ONE THAT LOWERS MAXIMUM HEALTH STOPS AT HALF. Ruled
        # 2026-10-08. See `MAX_AILMENT_MAX_HEALTH_REMOVED`.
        if (action == AILMENT_MAX_HEALTH_ACTION
                and max(low, high) > MAX_AILMENT_MAX_HEALTH_REMOVED):
            raise DataError(
                f"Enchantment Effects row {index}: {name} takes {low:g} to "
                f"{high:g} per cent off its carrier's maximum health. A "
                f"lowered maximum takes above 0 and up to "
                f"{MAX_AILMENT_MAX_HEALTH_REMOVED:g} per cent.")

    # A ROW THAT FIRES THE WEARER'S GADGETS AT ITS DEATH STATES 100, which the
    # game does not read: every cast is whole. Ruled 2026-10-19 (C2).
    if action == GADGETS_CAST_NOW_ACTION:
        if not (low == 100 and high == 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} fires its "
                f"wearer's gadgets and states the value {low:g} to "
                f"{high:g}. Each gadget casts at the whole of its damage, "
                f"so its value is 100 and nothing else.")

    # A CLEANSE'S VALUE IS 100, which the game does not read. Issue #1833.
    if action == CLEANSE_ACTION:
        if not (low == 100 and high == 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} cleanses and "
                f"states the value {low:g} to {high:g}. A cleanse has no "
                f"size, so its value is 100 and nothing else.")
        if scale_max_steps:
            raise DataError(
                f"Enchantment Effects row {index}: {name} cleanses and "
                f"states Scale Max Steps. It has no stacks, so it would be "
                f"dropped.")

    # A STATUS ACTION'S VALUE IS A CHANCE, above 0 and up to 100, or SECONDS,
    # above 0 and up to the bound. Issue #1833 group E part 1. Neither has
    # stacks.
    if action in ("apply_status_to_self_sized", APPLY_STATUS_SIZED_ACTION):
        if not (low == int(low) and high == int(high)
                and 1 <= low <= MAX_STATUS_SIZE
                and 1 <= high <= MAX_STATUS_SIZE):
            raise DataError(
                f"Enchantment Effects row {index}: {name} lays a status "
                f"at {low:g} to {high:g} times its ordinary "
                f"size. A size is a whole number from 1 to {MAX_STATUS_SIZE}.")
    if action in ("apply_status", "apply_status_to_self"):
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} applies a status "
                f"with a chance of {low:g} to {high:g}. A chance is above 0 "
                f"and up to 100, and 100 is always.")
    if action in ("apply_status_seconds", "apply_status_to_self_seconds"):
        if not (0 < low <= MAX_STATUS_SECONDS
                and 0 < high <= MAX_STATUS_SECONDS):
            raise DataError(
                f"Enchantment Effects row {index}: {name} applies a status "
                f"for {low:g} to {high:g} seconds. It lasts above 0 and up to "
                f"{MAX_STATUS_SECONDS:g}.")
    # A SLOW'S VALUE IS ITS STRENGTH IN PER CENT, above 0 and up to
    # Cripple's cap. Traps layer 3, ruled 2026-10-21 (M3).
    if action == SLOW_FOR_SECONDS_ACTION:
        if not (0 < low <= MAX_SLOW_PERCENT and 0 < high <= MAX_SLOW_PERCENT):
            raise DataError(
                f"Enchantment Effects row {index}: {name} slows by {low:g} "
                f"to {high:g} per cent. A slow is above 0 and up to "
                f"{MAX_SLOW_PERCENT:g}, Cripple's cap.")

    # A REMAINING DAMAGE ACTION'S VALUE IS A SHARE OF THAT DAMAGE, above 0 and
    # up to 100. Issue #1833 group D part 4.
    if action in REMAINING_DAMAGE_ACTIONS:
        if not (0 < low <= 100 and 0 < high <= 100):
            raise DataError(
                f"Enchantment Effects row {index}: {name} deals {low:g} to "
                f"{high:g} per cent of the remaining damage. It deals above 0 "
                f"and up to 100.")

    # A COOLDOWN REDUCTION'S VALUE IS SECONDS, above 0 and up to the bound.
    # Issue #1833, the cooldown reduction action.
    if action in COOLDOWN_REDUCE_ACTIONS:
        if not (0 < low <= MAX_COOLDOWN_REDUCE_SECONDS
                and 0 < high <= MAX_COOLDOWN_REDUCE_SECONDS):
            raise DataError(
                f"Enchantment Effects row {index}: {name} takes {low:g} to "
                f"{high:g} seconds off cooldowns. It takes above 0 and up "
                f"to {MAX_COOLDOWN_REDUCE_SECONDS:g} seconds.")
        if scale_max_steps:
            raise DataError(
                f"Enchantment Effects row {index}: {name} reduces cooldowns "
                f"and states Scale Max Steps. A reduction has no stacks, so "
                f"it would be dropped.")

    # A ROW'S OWN STACKS. Issue #1833, ruled 2026-09-23: the row's Action
    # Event grants a stack, Stack Seconds is how long they last and Scale
    # Max Steps the cap. All three are required, and none of them means
    # anything on any other stat row, so each is refused there rather than
    # dropped.
    stack_text = clean(_cell(raw, headers, "Stack Seconds"))
    stack_seconds = 0.0
    if not action and scale == "own_stacks":
        known_events = granting_events()
        if action_event not in known_events:
            raise DataError(
                f"Enchantment Effects row {index}: {name} counts its own "
                f"stacks and names the event {action_event or '(none)'!r} "
                f"to grant them. Known: {', '.join(sorted(known_events))}.")
        if not stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} counts its own "
                f"stacks and states no Stack Seconds for them to last.")
        stack_seconds = number(stack_text, "Stack Seconds", index)
        if not 0.0 < stack_seconds <= MAX_STACK_SECONDS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} has stacks lasting "
                f"{stack_seconds:g} seconds. They last above 0 and up to "
                f"{MAX_STACK_SECONDS:g}.")
        # NO CAP ON THE EVENTS WHOSE UNCAPPED STACKS LAPSE ALONE. Traps
        # layer 3, ruled 2026-10-21 (M4 (a)).
        if scale_max_steps < 1 \
                and action_event not in OWN_STACKS_LAPSING_ALONE_EVENTS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} counts its own "
                f"stacks and states no cap in Scale Max Steps. \"Up to 5 "
                f"stacks\" is 5.")
    elif action in PLACED_ACTIONS:
        # A STACK PLACED ON THE OTHER CHARACTER. Issue #1833, phase 2. The
        # window is required and the cap is not: "stacking indefinitely"
        # states none. Each stack takes away a share, so both ends are
        # above nought.
        if not stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} places {action} "
                f"and states no Stack Seconds for the stacks to last.")
        stack_seconds = number(stack_text, "Stack Seconds", index)
        if not 0.0 < stack_seconds <= MAX_STACK_SECONDS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} places stacks "
                f"lasting {stack_seconds:g} seconds. They last above 0 and "
                f"up to {MAX_STACK_SECONDS:g}.")
        if low <= 0 or high <= 0:
            raise DataError(
                f"Enchantment Effects row {index}: {name} places {action} "
                f"worth {low:g} to {high:g} a stack. A stack takes away a "
                f"share, so both ends are above nought.")
    elif action in (KEPT_STACKS_REPEAT_SKILL_ACTION,
                    KEPT_STACKS_FREE_NEXT_ABILITY_ACTION):
        # A KEPT STACK HAS NO LIFETIME. Ruled 2026-10-12, and for the second
        # action that gains them on 2026-10-17. Refused rather than
        # dropped, so that a row is never read as stating one.
        if stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} gains kept "
                f"stacks and states Stack Seconds. A kept stack has no "
                f"lifetime: it is held until a use spends it, the dungeon "
                f"is left, the wearer dies or the gear comes off. Leave "
                f"the column empty.")
    elif action == STUN_NEAR_THE_DYING_ACTION:
        # HOW LONG THE STUN LASTS. Ruled 2026-10-08: a chance and a duration
        # on one row, the duration here because the value is the chance.
        # Required, and bounded as a stated status's seconds are and not as
        # a stack's. No stack is behind it. See `MAX_STACK_SECONDS` for
        # everything else this column holds.
        if not stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} stuns the enemies "
                f"near an enemy that died and states no Stack Seconds. On "
                f"{action} that column is how long the stun lasts.")
        stack_seconds = number(stack_text, "Stack Seconds", index)
        if not 0.0 < stack_seconds <= MAX_STATUS_SECONDS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} stuns for "
                f"{stack_seconds:g} seconds. A stun lasts above 0 and up to "
                f"{MAX_STATUS_SECONDS:g}.")
    elif action == SLOW_FOR_SECONDS_ACTION:
        # HOW LONG THE SLOW LASTS. Traps layer 3, ruled 2026-10-21 (M3): the
        # duration here because the value is the strength, as the stun near
        # the dying's is; required, and bounded as a stated status's seconds
        # are.
        if not stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} slows and states "
                f"no Stack Seconds. On {action} that column is how long the "
                f"slow lasts.")
        stack_seconds = number(stack_text, "Stack Seconds", index)
        if not 0.0 < stack_seconds <= MAX_STATUS_SECONDS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} slows for "
                f"{stack_seconds:g} seconds. A slow lasts above 0 and up to "
                f"{MAX_STATUS_SECONDS:g}.")
    elif not action and scale == "consecutive_hits":
        # HITS IN A ROW ON ONE ENEMY. Issue #1833, phase 2. The event must
        # name who was struck, the cap is required, and there is no window.
        if action_event not in CONSECUTIVE_HIT_EVENTS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} counts hits in a "
                f"row and names the event {action_event or '(none)'!r} to "
                f"count them. Only an event naming who was struck can: "
                f"{', '.join(CONSECUTIVE_HIT_EVENTS)}.")
        if stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} counts hits in a "
                f"row and states Stack Seconds. The count has no timer "
                f"(ruled 2026-09-24), so they would be dropped.")
        if scale_max_steps < 1:
            raise DataError(
                f"Enchantment Effects row {index}: {name} counts hits in a "
                f"row and states no cap in Scale Max Steps. \"Up to 8 "
                f"stacks\" is 8.")
    elif not action:
        if stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} states Stack "
                f"Seconds and is not scaled by own_stacks, so they would "
                f"be dropped.")
        if action_event:
            raise DataError(
                f"Enchantment Effects row {index}: {name} changes a stat "
                f"and names the event {action_event!r}. A stat row reads "
                f"an event only to grant its own stacks, with the scale "
                f"own_stacks; here it would be dropped.")

    # "ABOVE 100" IS AN OFFSET OF 100. Issue #1686. Empty is none. An offset
    # needs a scale that reads one, and is a number of points from 0 up to
    # the point budget.
    offset_text = clean(_cell(raw, headers, "Scale Offset"))
    scale_offset = 0.0
    if offset_text:
        offset = number(offset_text, "Scale Offset", index)
        if scale not in SCALES_THAT_TAKE_AN_OFFSET:
            raise DataError(
                f"Enchantment Effects row {index}: {name} has an offset of "
                f"{offset:g} on the scale {scale or '(none)'!r}. Only "
                f"{', '.join(sorted(SCALES_THAT_TAKE_AN_OFFSET))} reads an "
                f"offset, so here it would be dropped.")
        if not 0 <= offset <= MAX_SCALE_OFFSET:
            raise DataError(
                f"Enchantment Effects row {index}: {name} has an offset of "
                f"{offset:g}. An offset is a number of points from 0 to "
                f"{MAX_SCALE_OFFSET}; leave the column empty for none.")
        scale_offset = offset

    # A TIMED ROW STATES ITS PERIOD, AND ONLY A TIMED ROW DOES. Issue
    # #1833: "Every 10 seconds" is 10. Refused either way round, so a
    # period is never written where nothing reads it, nor a clock left
    # with none.
    #
    # AND A ROW THAT GAINS KEPT STACKS DOES AS WELL. Ruled 2026-10-12. Its
    # Action Event is the use that spends the stacks, so its period cannot
    # be told from its event; the game reads Every Seconds for it whatever
    # the event is. Required there, with the same bound.
    every_text = clean(_cell(raw, headers, "Every Seconds"))
    every_seconds = 0.0
    if action == KEPT_STACKS_REPEAT_SKILL_ACTION:
        if not every_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} gains kept "
                f"stacks and states no Every Seconds, so it would never "
                f"gain one. On {action} that column is the period of the "
                f"gain.")
        every_seconds = number(every_text, "Every Seconds", index)
        if not 0.0 < every_seconds <= MAX_EVERY_SECONDS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} gains a kept "
                f"stack every {every_seconds:g} seconds. A period is above "
                f"0 and up to {MAX_EVERY_SECONDS:g}.")
    elif action_event == TIMED_EVENT:
        if not every_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} grants on "
                f"{TIMED_EVENT} and states no Every Seconds, so it "
                f"would never grant.")
        every_seconds = number(every_text, "Every Seconds", index)
        if not 0.0 < every_seconds <= MAX_EVERY_SECONDS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} grants every "
                f"{every_seconds:g} seconds. A period is above 0 and up "
                f"to {MAX_EVERY_SECONDS:g}.")
    elif every_text:
        raise DataError(
            f"Enchantment Effects row {index}: {name} states Every "
            f"Seconds on the event {action_event or '(none)'!r}. Only "
            f"{TIMED_EVENT} reads a period, so it would be dropped.")

    # AN EVERY-Nth ROW STATES N, AND ONLY IT DOES. Issue #1833, phase 2:
    # "Every third cast" is 3. A whole number from 2: "every 1st" is every
    # time, which a row would say without counting. An attack's value is
    # 100, "no damage"; the other two take something, so both ends are
    # above nought. Neither a window nor a cap means anything here.
    nth_text = clean(_cell(raw, headers, "Every Nth"))
    every_nth = 0
    if action in NTH_ACTIONS:
        if not nth_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} is the every-Nth "
                f"action {action} and states no Every Nth, so it would "
                f"never act.")
        nth = number(nth_text, "Every Nth", index)
        if nth != int(nth) or not 2 <= nth <= MAX_EVERY_NTH:
            raise DataError(
                f"Enchantment Effects row {index}: {name} acts every "
                f"{nth:g}th. N is a whole number from 2 to {MAX_EVERY_NTH}.")
        every_nth = int(nth)
        if action == "nth_attack_no_damage":
            if low != 100 or high != 100:
                raise DataError(
                    f"Enchantment Effects row {index}: {name} is "
                    f"{action} worth {low:g} to {high:g}. No damage is all "
                    f"of it: 100.")
        elif low <= 0 or high <= 0:
            raise DataError(
                f"Enchantment Effects row {index}: {name} is {action} "
                f"worth {low:g} to {high:g}. It takes something, so both "
                f"ends are above nought.")
        if stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} is {action} and "
                f"states Stack Seconds. A count has no window (ruled "
                f"2026-09-24), so they would be dropped.")
    elif action in NEARBY_ACTIONS and nth_text:
        # A NEARBY ACTION ON THE Nth OF ITS EVENTS INSIDE A WINDOW. Issue
        # #1833 group E part 3, ruled 2026-10-02: "Every 3 blocks in quick
        # succession" is Every Nth 3 with Stack Seconds 3, and the count
        # starts again once it acts. The window is required: "in quick
        # succession" without one is no window at all.
        nth = number(nth_text, "Every Nth", index)
        if nth != int(nth) or not 2 <= nth <= MAX_EVERY_NTH:
            raise DataError(
                f"Enchantment Effects row {index}: {name} acts every "
                f"{nth:g}th. N is a whole number from 2 to {MAX_EVERY_NTH}.")
        every_nth = int(nth)
        if not stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} counts {every_nth} "
                f"of its events and states no Stack Seconds for the window "
                f"they fall in.")
        stack_seconds = number(stack_text, "Stack Seconds", index)
        if not 0.0 < stack_seconds <= MAX_STACK_SECONDS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} counts its events in "
                f"a window of {stack_seconds:g} seconds. It is above 0 and up "
                f"to {MAX_STACK_SECONDS:g}.")
    elif action == GADGET_FIRES_AT_TRAP_ENEMY_ACTION and nth_text:
        # A GADGET'S SHOT ON THE Nth TRAP TRIGGERED, WITH NO WINDOW. Ruled
        # 2026-10-22 (traps layer 6, J4, J10): "For every 3 traps
        # triggered" is Every Nth 3, one count per player that starts again
        # after each third and at death. A window would be one no sentence
        # states, so Stack Seconds is refused.
        nth = number(nth_text, "Every Nth", index)
        if nth != int(nth) or not 2 <= nth <= MAX_EVERY_NTH:
            raise DataError(
                f"Enchantment Effects row {index}: {name} acts every "
                f"{nth:g}th. N is a whole number from 2 to {MAX_EVERY_NTH}.")
        every_nth = int(nth)
        if stack_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} counts {every_nth} "
                f"traps triggered and states Stack Seconds. The count has no "
                f"window (ruled 2026-10-22, J4), so they would be dropped.")
    elif nth_text:
        raise DataError(
            f"Enchantment Effects row {index}: {name} states Every Nth and "
            f"is not an every-Nth action, so it would be dropped. Those "
            f"actions: {', '.join(NTH_ACTIONS)}; and a nearby action, "
            f"{', '.join(NEARBY_ACTIONS)}, with a window in Stack Seconds.")
    elif action in NEARBY_ACTIONS and stack_text:
        raise DataError(
            f"Enchantment Effects row {index}: {name} is a nearby action and "
            f"states Stack Seconds with no Every Nth, so they would be "
            f"dropped.")

    # AND STACK SECONDS ON ANY OTHER ACTION. Issue #2337. The branches
    # above read the column for the actions in
    # `ACTIONS_THAT_READ_STACK_SECONDS` and refuse it on the ones that
    # count without a window; every other action, `apply_status` among
    # them, carried nothing and wrote 0.0, so a row stating "lasts 3
    # seconds" there lasted nothing and said nothing.
    if (action and stack_text
            and action not in ACTIONS_THAT_READ_STACK_SECONDS):
        raise DataError(
            f"Enchantment Effects row {index}: {name} states Stack Seconds "
            f"on the action {action}, which does not read it, so it would "
            f"be dropped. The column is read on a stat row scaled by "
            f"own_stacks and on these actions: "
            f"{', '.join(sorted(ACTIONS_THAT_READ_STACK_SECONDS))}.")

    # THE STEP'S HIGH END, WHEN THE STEP ROLLS WITH THE VALUE. Issue #1833,
    # the kill counter: "for every 100,000-500,000 kills" states the step
    # as a range, and the item's roll picks one step where it picks the
    # value, so the hover text, which shows both numbers at that roll, and
    # the effect agree. The pair is a range the sentence states, low end
    # first, and the high end is inside the scale's bounds.
    step_high_text = clean(_cell(raw, headers, "Scale Step High"))
    scale_step_high = 0.0
    if action in USE_INCREASED_DAMAGE_ACTIONS:
        # ON THESE TWO ACTIONS THE PAIR IS THE SIZE OF THE INCREASE AND NOT
        # A STEP. Ruled 2026-10-06. See `USE_INCREASED_DAMAGE_ACTIONS`. It
        # is required, because a row without it would roll and add
        # nothing; it is refused with a Scale, because then it would be
        # read as that scale's step; and it is a range the sentence
        # states, as a step's range is.
        if scale:
            raise DataError(
                f"Enchantment Effects row {index}: {name} rolls a use's "
                f"increased damage and states the scale {scale!r}. Its "
                f"Scale Step is the size of the increase and not a step, "
                f"so the Scale column must be empty.")
        step_text = clean(_cell(raw, headers, "Scale Step"))
        if not step_text or not step_high_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} rolls a use's "
                f"increased damage and does not state how large the "
                f"increase is. On the action {action!r} that is Scale Step "
                f"and Scale Step High, the sentence's second range.")
        scale_step = number(step_text, "Scale Step", index)
        scale_step_high = number(step_high_text, "Scale Step High", index)
        if not 0.0 < scale_step < scale_step_high <= MAX_USE_INCREASE:
            raise DataError(
                f"Enchantment Effects row {index}: {name} gives an "
                f"increase from {scale_step:g} to {scale_step_high:g}. The "
                f"low end is above 0, the high end above it and at most "
                f"{MAX_USE_INCREASE:g}.")
        if (scale_step, scale_step_high) not in enchantment_ranges(words[name]):
            raise DataError(
                f"Enchantment Effects row {index}: {name} gives an "
                f"increase from {scale_step:g} to {scale_step_high:g}, "
                f"which its words {words[name]!r} do not state as a range, "
                f"so the hover text would show one size and the effect "
                f"use another.")
    elif step_high_text:
        scale_step_high = number(step_high_text, "Scale Step High", index)
        if not scale:
            raise DataError(
                f"Enchantment Effects row {index}: {name} states Scale "
                f"Step High and no scale, so it would be dropped.")
        top = SCALES[scale][1]
        if not scale_step < scale_step_high <= top:
            raise DataError(
                f"Enchantment Effects row {index}: {name} steps from "
                f"{scale_step:g} to {scale_step_high:g}. The high end is "
                f"above the step and at most {top:g}.")
        if (scale_step, scale_step_high) not in enchantment_ranges(words[name]):
            raise DataError(
                f"Enchantment Effects row {index}: {name} steps from "
                f"{scale_step:g} to {scale_step_high:g}, which its words "
                f"{words[name]!r} do not state as a range, so the hover "
                f"text would show one step and the effect use another.")

    # THE STACKS' HIGH END, WHEN HOW LONG THEY LAST ROLLS WITH THE VALUE.
    # Issue #1833, ruled 2026-09-30 under the owner's delegation, following
    # Scale Step High: "Killing an enemy triggers a 1-2 second global
    # cooldown on all your skills" states the time as a range, and the
    # item's roll picks one time where it picks the value. The pair is a
    # range the sentence states, low end first, and the high end is inside
    # the bound every stack's time keeps.
    stack_high_text = clean(_cell(raw, headers, "Stack Seconds High"))
    stack_seconds_high = 0.0
    if stack_high_text:
        stack_seconds_high = number(stack_high_text, "Stack Seconds High", index)
        if not stack_seconds:
            raise DataError(
                f"Enchantment Effects row {index}: {name} states Stack "
                f"Seconds High and no Stack Seconds, so it would be dropped.")
        # A STUN'S SECONDS KEEP THEIR OWN BOUND AT THE HIGH END TOO. Ruled
        # 2026-10-08. See `STUN_NEAR_THE_DYING_ACTION`.
        longest = (MAX_STATUS_SECONDS
                   if action in (STUN_NEAR_THE_DYING_ACTION,
                                 SLOW_FOR_SECONDS_ACTION)
                   else MAX_STACK_SECONDS)
        if not stack_seconds < stack_seconds_high <= longest:
            raise DataError(
                f"Enchantment Effects row {index}: {name} has stacks lasting "
                f"{stack_seconds:g} to {stack_seconds_high:g} seconds. The "
                f"high end is above Stack Seconds and at most "
                f"{longest:g}.")
        if (stack_seconds, stack_seconds_high) not in enchantment_ranges(words[name]):
            raise DataError(
                f"Enchantment Effects row {index}: {name} has stacks lasting "
                f"{stack_seconds:g} to {stack_seconds_high:g} seconds, which "
                f"its words {words[name]!r} do not state as a range, so the "
                f"hover text would show one time and the effect use another.")

    # A SECOND CONDITION, WHICH MUST HOLD AS WELL. Issue #1833, ruled
    # 2026-09-30 under the owner's delegation: "While stationary you take
    # 20%-35% increased damage from ranged attacks" names two states, and the
    # literal reading is both. Only a stat row carries it: an action row's
    # condition is copied onto its action one at a time.
    condition2 = clean(_cell(raw, headers, "Condition 2")).lower()
    condition_value2 = 0.0
    value2_text = clean(_cell(raw, headers, "Condition Value 2"))
    if condition2:
        if condition2 not in CONDITIONS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} names the second "
                f"condition {condition2!r}, which the game cannot judge. "
                f"Known: {', '.join(sorted(CONDITIONS))}.")
        if not condition:
            raise DataError(
                f"Enchantment Effects row {index}: {name} states Condition 2 "
                f"and no Condition. Write a single condition in Condition.")
        if condition2 == condition:
            raise DataError(
                f"Enchantment Effects row {index}: {name} names "
                f"{condition!r} as both conditions.")
        # AN ACTION MAY CARRY A SECOND CONDITION since the War vow options'
        # second slice (V2b, 2026-11-25): Warlord's armour removal holds "above
        # 50% Fervour" with "Two-handed weapons only". Both are judged when the
        # event fires (`UCataclysmAbilitySystemComponent::PoolActionAllowed`).
        # NOT AN EXECUTE: its one condition is its share of health, judged after
        # the hit.
        if action == EXECUTE_TARGET_ACTION:
            raise DataError(
                f"Enchantment Effects row {index}: {name} executes and states "
                f"Condition 2. An execute carries one condition, its share of "
                f"health, judged after the hit.")
        if CONDITIONS[condition2] is None:
            if value2_text:
                raise DataError(
                    f"Enchantment Effects row {index}: {name} carries the "
                    f"second condition {condition2!r} and a value of "
                    f"{value2_text!r}. That condition compares nothing, so "
                    f"the value would be ignored. Leave the column empty.")
        else:
            condition_value2 = number(value2_text, "Condition Value 2", index)
            low2, high2, units2 = CONDITIONS[condition2]
            if not low2 <= condition_value2 <= high2:
                raise DataError(
                    f"Enchantment Effects row {index}: {name} has a second "
                    f"condition value of {condition_value2}, and "
                    f"{condition2!r} takes {units2} between {low2:g} and "
                    f"{high2:g}.")
    elif value2_text:
        raise DataError(
            f"Enchantment Effects row {index}: {name} states Condition Value "
            f"2 and no Condition 2, so it would be dropped.")

    # THE CONDITION'S HIGH END, WHEN THE THRESHOLD ROLLS WITH THE VALUE.
    # Issue #1833, ruled 2026-09-30, following Scale Step High: "Your
    # abilities are free when above 80%-95% hp" states the threshold as a
    # range, and the item's roll picks one threshold where it picks the
    # value. The pair is a range the sentence states, low end first, inside
    # the condition's own bounds. Only a stat row's threshold rolls.
    condition_high_text = clean(_cell(raw, headers, "Condition Value High"))
    condition_value_high = 0.0
    if condition_high_text:
        condition_value_high = number(condition_high_text,
                                      "Condition Value High", index)
        if not condition or CONDITIONS[condition] is None:
            raise DataError(
                f"Enchantment Effects row {index}: {name} states Condition "
                f"Value High and no condition that compares a value, so it "
                f"would be dropped.")
        if action:
            raise DataError(
                f"Enchantment Effects row {index}: {name} moves {action} and "
                f"states Condition Value High. An action's threshold does "
                f"not roll.")
        top_condition = CONDITIONS[condition][1]
        if not condition_value < condition_value_high <= top_condition:
            raise DataError(
                f"Enchantment Effects row {index}: {name} has a threshold of "
                f"{condition_value:g} to {condition_value_high:g}. The high "
                f"end is above Condition Value and at most "
                f"{top_condition:g}.")
        if (condition_value, condition_value_high) not in enchantment_ranges(words[name]):
            raise DataError(
                f"Enchantment Effects row {index}: {name} has a threshold of "
                f"{condition_value:g} to {condition_value_high:g}, which its "
                f"words {words[name]!r} do not state as a range, so the "
                f"hover text would show one threshold and the effect use "
                f"another.")

    # HOW LONG A TRIGGER WAITS AFTER IT FIRES. Issue #1833 group D, ruled
    # 2026-09-30 under the owner's delegation. Only an action row that
    # makes something happen takes one, and not on the timed event, whose
    # own period is its spacing. EMPTY IS THE DEFAULT, written here so the
    # table says what each row does: `DEFAULT_TRIGGER_COOLDOWN` on a
    # hit-fired event and none elsewhere. An explicit 0 is none, for a
    # sentence that says every hit.
    cooldown_text = clean(_cell(raw, headers, "Trigger Cooldown"))
    trigger_cooldown = 0.0
    # AN OWN STACK TAKES ONE WHEN ITS SENTENCE STATES ONE, ruled 2026-09-30
    # for the Demon King's Regalia's "(5 minute cd)", and never by default.
    own_stack_grant = not action and scale == "own_stacks"
    if cooldown_text:
        trigger_cooldown = number(cooldown_text, "Trigger Cooldown", index)
        if not ((action and takes_a_trigger_cooldown(action)) or own_stack_grant) \
                or action_event == TIMED_EVENT:
            raise DataError(
                f"Enchantment Effects row {index}: {name} states a Trigger "
                f"Cooldown of {trigger_cooldown:g} seconds. Only an action "
                f"row that moves a pool, resets or reduces a cooldown or "
                f"applies a random damage over time on an event takes one; "
                f"a row that counts or stacks never waits, so it would be "
                f"dropped.")
        if not 0 <= trigger_cooldown <= MAX_TRIGGER_COOLDOWN:
            raise DataError(
                f"Enchantment Effects row {index}: {name} has a Trigger "
                f"Cooldown of {trigger_cooldown:g} seconds. It is from 0, "
                f"which is none, up to {MAX_TRIGGER_COOLDOWN:g}.")
    elif action and takes_a_trigger_cooldown(action) \
            and action not in ACTIONS_WITH_NO_DEFAULT_TRIGGER_COOLDOWN \
            and action_event in HIT_FIRED_EVENTS:
        trigger_cooldown = DEFAULT_TRIGGER_COOLDOWN

    # THE THRESHOLD ITS EVENT CROSSES. Issue #1833 group D part 2, ruled
    # 2026-09-30: only `health_falls_below` reads it, so it is required there
    # and refused everywhere else, on an action row and an own stack alike.
    event_value_text = clean(_cell(raw, headers, "Event Value"))
    event_value = 0.0
    if action_event == THRESHOLD_EVENT:
        if not event_value_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} hangs on "
                f"{THRESHOLD_EVENT} and states no Event Value. It is the share "
                f"of maximum health, in per cent, that health falls below.")
        event_value = number(event_value_text, "Event Value", index)
        if not 0 < event_value < 100:
            raise DataError(
                f"Enchantment Effects row {index}: {name} has an Event Value "
                f"of {event_value:g}. It is a share of maximum health above 0 "
                f"and below 100.")
    elif action == GADGET_FIRES_AT_TRAP_ENEMY_ACTION:
        # OR THE METRES FROM THE TRAP A GADGET MUST STAND WITHIN, for a
        # gadget's shot at a trap's enemy: empty or nought is any distance.
        # Ruled 2026-10-22 (traps layer 6, J1, J5, J10).
        if event_value_text:
            event_value = number(event_value_text, "Event Value", index)
            if not 0 <= event_value <= MAX_GADGET_SHOT_AT_TRAP_ENEMY_METRES:
                raise DataError(
                    f"Enchantment Effects row {index}: {name} fires the "
                    f"gadget within {event_value:g} metres of a trap. The "
                    f"radius is 0, any distance, up to "
                    f"{MAX_GADGET_SHOT_AT_TRAP_ENEMY_METRES:g}.")
    elif action_event == SPENT_EVENT:
        # OR THE FERVOUR ONE PAYMENT MUST SPEND, "after you spend 10 or more
        # Fervour at once": empty or nought is any spend. The Death trees' slice
        # C3a (2026-11-25); `UCataclysmAbilitySystemComponent::
        # EventThresholdCrossed` reads it.
        if event_value_text:
            event_value = number(event_value_text, "Event Value", index)
            if not 0 <= event_value <= MAX_FERVOUR_SPENT_AT_ONCE:
                raise DataError(
                    f"Enchantment Effects row {index}: {name} wants "
                    f"{event_value:g} Fervour spent at once. It is 0, any "
                    f"spend, up to {MAX_FERVOUR_SPENT_AT_ONCE:g}.")
    elif action == SMITE_NEARBY_BY_HIT_ACTION:
        # OR THE RADIUS OF A NOVA SIZED BY A HIT, in metres, required. V2b,
        # 2026-11-25. See `SMITE_NEARBY_BY_HIT_ACTION`.
        if not event_value_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} deals a share of a "
                f"hit to the enemies nearby and states no Event Value. It is the "
                f"radius in metres.")
        event_value = number(event_value_text, "Event Value", index)
        if not 0 < event_value <= MAX_SMITE_NEARBY_BY_HIT_METRES:
            raise DataError(
                f"Enchantment Effects row {index}: {name} reaches "
                f"{event_value:g} metres. The radius is above 0 and up to "
                f"{MAX_SMITE_NEARBY_BY_HIT_METRES:g}.")
    elif action == TITANFALL_ACTION:
        # OR TITANFALL'S SHARE OF MAXIMUM HEALTH A BIG HIT MUST TAKE, in per
        # cent, required. 2026-11-25. See `TITANFALL_ACTION`.
        if not event_value_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} acts on a big hit and "
                f"states no Event Value. It is the share of maximum health, in "
                f"per cent, the hit must take.")
        event_value = number(event_value_text, "Event Value", index)
        if not 0 < event_value <= MAX_TITANFALL_SHARE_PERCENT:
            raise DataError(
                f"Enchantment Effects row {index}: {name} asks a big hit for "
                f"{event_value:g} per cent of maximum health. It is above 0 and "
                f"up to {MAX_TITANFALL_SHARE_PERCENT:g}.")
    elif action == SCRAP_MINE_ACTION:
        # OR A SCRAP TRAP'S TRIGGER RADIUS, in metres, required. V5b1,
        # 2026-11-25. See `SCRAP_MINE_ACTION`.
        if not event_value_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} leaves a scrap trap "
                f"and states no Event Value. It is the trigger radius in "
                f"metres.")
        event_value = number(event_value_text, "Event Value", index)
        if not 0 < event_value <= MAX_SCRAP_MINE_METRES:
            raise DataError(
                f"Enchantment Effects row {index}: {name} leaves a scrap trap "
                f"with a radius of {event_value:g} metres. It is above 0 and up "
                f"to {MAX_SCRAP_MINE_METRES:g}.")
    elif action in (COPY_LAST_TRAP_AT_KILL_ACTION, CROSSFIRE_ACTION):
        # OR THE METRES FROM A DEVICE, required: Where They Fall's to the one
        # killed, Crossfire's to the enemy struck. V5a, 2026-11-25.
        bound = (MAX_COPY_LAST_TRAP_AT_KILL_METRES
                 if action == COPY_LAST_TRAP_AT_KILL_ACTION
                 else MAX_CROSSFIRE_METRES)
        if not event_value_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} acts as {action} and "
                f"states no Event Value. It is the metres from a device.")
        event_value = number(event_value_text, "Event Value", index)
        if not 0 < event_value <= bound:
            raise DataError(
                f"Enchantment Effects row {index}: {name} reaches "
                f"{event_value:g} metres from a device. It is above 0 and up "
                f"to {bound:g}.")
    elif action == COUNTERFIRE_ACTION:
        # OR COUNTERFIRE'S DEVICES FOR EACH ANSWER, required, a whole number
        # from 1 to the bound. V4b, 2026-11-25. See `COUNTERFIRE_ACTION`.
        if not event_value_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} answers an evaded "
                f"blow and states no Event Value. It is the devices for each "
                f"answer.")
        event_value = number(event_value_text, "Event Value", index)
        if (event_value != int(event_value)
                or not 1 <= event_value <= MAX_COUNTERFIRE_DEVICES_PER_ANSWER):
            raise DataError(
                f"Enchantment Effects row {index}: {name} answers once for every "
                f"{event_value:g} devices. It is a whole number from 1 to "
                f"{MAX_COUNTERFIRE_DEVICES_PER_ANSWER:g}.")
    elif action == TAUNT_NEAR_TRAP_ACTION:
        # OR THE RADIUS OF BAITED'S TAUNT, in metres around the trap, required.
        # V4a, 2026-11-25. See `TAUNT_NEAR_TRAP_ACTION`.
        if not event_value_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} taunts the enemies "
                f"near a trap and states no Event Value. It is the radius in "
                f"metres.")
        event_value = number(event_value_text, "Event Value", index)
        if not 0 < event_value <= MAX_TAUNT_NEAR_TRAP_METRES:
            raise DataError(
                f"Enchantment Effects row {index}: {name} taunts within "
                f"{event_value:g} metres. The radius is above 0 and up to "
                f"{MAX_TAUNT_NEAR_TRAP_METRES:g}.")
    elif action == FRENZY_CHAIN_ACTION:
        # OR THE RADIUS OF A FRENZY CHAIN, in metres around the enemy struck,
        # required. V3b, 2026-11-25. See `FRENZY_CHAIN_ACTION`.
        if not event_value_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} chains an attack and "
                f"states no Event Value. It is the radius in metres.")
        event_value = number(event_value_text, "Event Value", index)
        if not 0 < event_value <= MAX_FRENZY_CHAIN_METRES:
            raise DataError(
                f"Enchantment Effects row {index}: {name} chains "
                f"{event_value:g} metres. The radius is above 0 and up to "
                f"{MAX_FRENZY_CHAIN_METRES:g}.")
    elif action == RETALIATION_PULSE_ACTION:
        # OR THE RADIUS OF A RETALIATION PULSE, in metres, required, and its
        # share bounded. V3a, 2026-11-25. See `RETALIATION_PULSE_ACTION`.
        if not event_value_text:
            raise DataError(
                f"Enchantment Effects row {index}: {name} pays a retaliation "
                f"pulse and states no Event Value. It is the radius in metres.")
        event_value = number(event_value_text, "Event Value", index)
        if not 0 < event_value <= MAX_RETALIATION_PULSE_METRES:
            raise DataError(
                f"Enchantment Effects row {index}: {name} reaches "
                f"{event_value:g} metres. The radius is above 0 and up to "
                f"{MAX_RETALIATION_PULSE_METRES:g}.")
    elif event_value_text:
        raise DataError(
            f"Enchantment Effects row {index}: {name} states an Event Value on "
            f"the event {action_event or '(none)'!r}. Only {THRESHOLD_EVENT}, "
            f"{SPENT_EVENT} and {GADGET_FIRES_AT_TRAP_ENEMY_ACTION} read one, "
            f"so it would be dropped.")

    # THE AILMENT A REMAINING DAMAGE ACTION IS LIMITED TO. Issue #1833 group D
    # part 4, ruled 2026-10-01: required on `dot_remaining_target`, allowed on
    # `dot_remaining_nearby`, and refused everywhere else, as Event Value is.
    ailment = clean(_cell(raw, headers, "Ailment"))
    # AND THE STATUS A STATUS ACTION APPLIES, in the same cell. Issue #1833
    # group E part 1, ruled 2026-10-01: required on both status actions.
    if action in ALL_APPLY_STATUS_ACTIONS:
        known = {"apply_status_seconds": APPLY_STATUSES_FOR_SECONDS,
                 "apply_status_to_self_seconds": APPLY_STATUSES_TO_SELF_FOR_SECONDS,
                 "apply_status_to_self_sized": (APPLIED_DOT_STATUS,),
                 # AND `apply_status_sized` NAMES A DAMAGE OVER TIME, whose
                 # size is what a tick deals. Traps layer 3, 2026-10-21.
                 APPLY_STATUS_SIZED_ACTION: AILMENTS,
                 # AND `apply_status` ALONE MAY NAME ALL FIVE DAMAGE OVER
                 # TIME AILMENTS AT ONCE. The owner, 2026-10-08.
                 "apply_status": APPLY_STATUSES + (ALL_DOTS_STATUS,
                                                   SILENCE_STATUS),
                 # AND `apply_status_to_self` ALONE MAY NAME THE STATUSES
                 # THAT ARE ONLY FOR A WEARER. Ruled 2026-10-12.
                 "apply_status_to_self": APPLY_STATUSES + APPLY_STATUSES_TO_SELF_ONLY,
                 }.get(action, APPLY_STATUSES)
        if ailment not in known:
            raise DataError(
                f"Enchantment Effects row {index}: {name} applies the status "
                f"{ailment or '(none)'!r}, which {action} cannot. Known: "
                f"{', '.join(known)}.")
    elif action in (KEPT_STACKS_REPEAT_SKILL_ACTION,
                    KEPT_STACKS_FREE_NEXT_ABILITY_ACTION):
        # WHAT THE SENTENCE CALLS THE STACKS, in the same cell. Ruled
        # 2026-10-12. Required, and a name the row's own words write
        # between single quotes: "a stack of 'Temporal Flux.'" gives
        # Temporal Flux, the full stop left out. The same for the second
        # action that gains kept stacks, ruled 2026-10-17: "'Void.'" gives
        # Void, which is why that sentence's double quotes are reworded
        # (O10).
        if not ailment:
            raise DataError(
                f"Enchantment Effects row {index}: {name} gains kept "
                f"stacks and states no name for them in Ailment. On "
                f"{action} that cell is what the sentence calls the "
                f"stacks, which the game shows with their count.")
        if ailment.endswith(".") or not re.search(
                r"'" + re.escape(ailment) + r"\.?'", words[name]):
            raise DataError(
                f"Enchantment Effects row {index}: {name} calls its kept "
                f"stacks {ailment!r}, which its words {words[name]!r} do "
                f"not write between single quotes. Write the name as the "
                f"sentence gives it, without its full stop.")
    elif ailment:
        if action not in REMAINING_DAMAGE_ACTIONS \
                and action not in AILMENT_RIDER_ACTIONS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} names the ailment "
                f"{ailment!r} on {action or stat!r}. Only "
                f"{', '.join(REMAINING_DAMAGE_ACTIONS + ALL_APPLY_STATUS_ACTIONS + AILMENT_RIDER_ACTIONS)} "
                f"read one, so it would be dropped.")
        if action == AILMENT_DETONATION_ACTION:
            if ailment not in DETONATING_AILMENTS:
                raise DataError(
                    f"Enchantment Effects row {index}: {name} makes the ailment "
                    f"{ailment!r} detonate. The game detonates only "
                    f"{', '.join(DETONATING_AILMENTS)}, so the row would do "
                    f"nothing.")
        elif action == AILMENT_SPREAD_ACTION and ailment in SPREADING_SHARE_AILMENTS:
            pass
        elif ailment not in AILMENTS:
            raise DataError(
                f"Enchantment Effects row {index}: {name} names the ailment "
                f"{ailment!r}, which is not one the game has. Known: "
                f"{', '.join(AILMENTS)}.")
    elif action == "dot_remaining_target":
        raise DataError(
            f"Enchantment Effects row {index}: {name} deals remaining damage on "
            f"the event's target and names no Ailment. Name one of "
            f"{', '.join(AILMENTS)}.")
    elif action in AILMENT_RIDER_ACTIONS:
        raise DataError(
            f"Enchantment Effects row {index}: {name} hangs a number on an "
            f"ailment and names no Ailment. Name one of "
            f"{', '.join(AILMENTS)}.")

    # THE SHARE OF ITS DAMAGE A REPEAT DEALS, in percent. Mechanism B2, ruled
    # 2026-10-05: "Each skill has a 20%-40% chance to cast a duplicate at 50%
    # damage" is 50. Read by `repeat_skill` alone and refused everywhere
    # else, as Event Value is. An empty cell is written as 0, which the
    # game reads as the whole of the damage.
    share_text = clean(_cell(raw, headers, "Damage Share"))
    damage_share = 0.0
    if share_text:
        if action != REPEAT_SKILL_ACTION:
            raise DataError(
                f"Enchantment Effects row {index}: {name} states a Damage "
                f"Share on {action or stat!r}. Only {REPEAT_SKILL_ACTION} "
                f"reads one, so it would be dropped.")
        damage_share = number(share_text, "Damage Share", index)
        if not 0 < damage_share <= 100:
            raise DataError(
                f"Enchantment Effects row {index}: {name} repeats a skill at "
                f"{damage_share:g} per cent of its damage. A share is above 0 "
                f"and up to 100, and an empty cell is the whole.")

    # A RANGE THE SENTENCE MARKS AS ROLLING DOWN IS WRITTEN THE OTHER WAY
    # ROUND. Every pair above was checked in the sentence's own order, which
    # is how the sheet states it. The game reads the first of a pair at a
    # roll of 0 and the second at a roll of 1, so a range whose best or
    # harshest figure is its FIRST number leaves here with its ends
    # exchanged. See `ranges_rolling_down`. LAST, so that every check above
    # reads the pair as a person wrote it.
    marked = {
        stated for place, stated
        in enumerate(enchantment_ranges(words[name]), start=1)
        if place in rolls_down[name]}

    if value_as_stated is not None and value_as_stated in marked:
        low, high = high, low
    if scale_step_high and (scale_step, scale_step_high) in marked:
        scale_step, scale_step_high = scale_step_high, scale_step
    if stack_seconds_high and (stack_seconds, stack_seconds_high) in marked:
        stack_seconds, stack_seconds_high = stack_seconds_high, stack_seconds
    if condition_value_high and (condition_value, condition_value_high) in marked:
        condition_value, condition_value_high = condition_value_high, condition_value

    return {
        "Enchantment": name,
        "Stat": stat,
        "ValueKind": kind,
        "ValueLow": low,
        "ValueHigh": high,
        "RequiredTags": clean(_cell(raw, headers, "Required Tags")),
        "Condition": condition,
        "ConditionValue": condition_value,
        "Scale": scale,
        "ScaleStep": scale_step,
        "Action": action,
        "ActionEvent": action_event,
        "FractionOf": fraction_of,
        "ScaleMaxSteps": scale_max_steps,
        "StackSeconds": stack_seconds,
        "ScaleOffset": scale_offset,
        "EverySeconds": every_seconds,
        "EveryNth": every_nth,
        "ScaleStepHigh": scale_step_high,
        "StackSecondsHigh": stack_seconds_high,
        "Condition2": condition2,
        "ConditionValue2": condition_value2,
        "ConditionValueHigh": condition_value_high,
        "TriggerCooldown": trigger_cooldown,
        "EventValue": event_value,
        "Ailment": ailment,
        "DamageShare": damage_share,
        # EVERY TIME, ON EVERY ENCHANTMENT ROW. The Death trees' slice C6b3
        # (2026-11-25): only a Passive Effects row states a chance (Tear Loose,
        # `_chance_percent`), and the Enchantment Effects sheet has no such
        # column. Written so `FCataclysmEnchantmentEffectRow::ChancePercent`,
        # which a passive row's copy fills, is a column of this table as every
        # property of its struct must be.
        "ChancePercent": 0.0,
    }


def enchantment_effects(book) -> list[dict]:
    """What an enchantment grants, one stat effect per row. Issue #45.

    AN ENCHANTMENT SAYS WHAT IT DOES IN ENGLISH AND THIS SAYS IT IN NUMBERS. The
    Enchantments sheet carries a sentence written for a player -- "Double your
    energy shield" -- and no stat name, no bucket and no value. This sheet is
    where those are written, in the shape of the `Passive Effects` sheet, which
    the project owner chose on 2026-08-25 as the place a node's numbers live.

    A ROW NAMES ITS ENCHANTMENT BY THE ROW NAME AN ITEM STORES, and repeats the
    enchantment's own words in an `Effect` column that has to match them
    exactly. The name is what the game reads; the words are what a person
    editing the sheet reads. Requiring both means an enchantment that is
    reworded has to be read again here before its numbers are trusted.

    A ROW HERE IS OPTIONAL. An enchantment with no row grants nothing, which is
    what every enchantment did before this sheet existed.

    THREE KINDS OF ROW ARE REFUSED, each for a stated reason:

      a range the      the owner ruled on 2026-09-11 that an enchantment rolls
      words do not     a value evenly inside its range, and the game shows that
      state            number in place of the range, so the two values must be
                       a range the sentence states: its first number as Value
                       Low and its second as Value High, with the row's sign
      a range on a     no item records a roll for a set's 6-piece or 10-piece
      set row          row, because a piece records only the set's lowest
                       threshold row, so each of a set's rows states one number
      half a set       a set's first bonus and its drawback turn on together at
                       the same threshold, so a set is written whole or not at
                       all: one half alone would be a bonus with no cost, or a
                       cost with no bonus

    A ROW NAME IS THE ENCHANTMENT WITH `#1`, `#2` AND SO ON AFTER IT, as in
    `Passive Effects`, so one enchantment can grant two stats. No enchantment
    row name can contain a number sign, because `row_name` keeps only letters,
    digits and underscores.
    """
    rows = list(book["Enchantment Effects"].iter_rows(values_only=True))
    headers = _header_index(rows, "Enchantment Effects")

    # EVERY ENCHANTMENT THIS SHEET MAY NAME, with its words and its type, read
    # by the same function that writes the two enchantment CSVs. So a name here
    # is checked against exactly the name an item stores.
    words: dict[str, str] = {}
    types: dict[str, str] = {}
    #: The places of each sentence's ranges that roll downward. See
    #: `ranges_rolling_down`.
    rolls_down: dict[str, frozenset[int]] = {}

    # AND EVERY SET'S ROWS, GATHERED BY IDENTIFIER. The check at the end refuses
    # half a set, and to do that it has to know which of a set's bonus rows
    # comes first and which row is that set's drawback.
    bonuses: dict[int, list[tuple[int, str]]] = {}
    drawbacks: dict[int, list[str]] = {}

    for negative in (False, True):
        for row in enchantments(book, negative=negative):
            words[row["Name"]] = row["Effect"]
            types[row["Name"]] = row["EnchantmentType"]
            rolls_down[row["Name"]] = ranges_rolling_down(
                row["RollsDown"], row["Effect"])

            set_id = enchantment_set_id(row)
            if not set_id:
                continue
            if negative:
                drawbacks.setdefault(set_id, []).append(row["Name"])
            else:
                threshold = set_piece_threshold(row["Effect"])
                bonuses.setdefault(set_id, []).append(
                    (UNREACHABLE_THRESHOLD if threshold is None else threshold,
                     row["Name"]))

    out = []
    counts: dict[str, int] = {}
    for index, raw in enumerate(rows[1:], start=2):
        name = clean(_cell(raw, headers, "Enchantment"))
        if not name:
            continue

        row = _effect_row(raw, headers, index, name, words, types,
                          rolls_down)
        counts[name] = counts.get(name, 0) + 1
        out.append({"Name": f"{name}#{counts[name]}", **row})

    # THE SAME ENCHANTMENT AND THE SAME STAT TWICE IS A MISTAKE RATHER THAN A
    # DOUBLE HELPING, unless the two are conditioned or scaled differently. The
    # rule and its reasons are the ones `passive_effects` states for a node.
    # ONE NEXT-USE ROW PER ENCHANTMENT AND ACTION, because the game keys a
    # row's held charges by exactly that pair. Issue #1833, phase 2.
    next_use: dict[tuple, int] = {}
    for row in out:
        if row["Action"] in NEXT_USE_ACTIONS:
            pair = (row["Enchantment"], row["Action"])
            next_use[pair] = next_use.get(pair, 0) + 1
    doubled = sorted(pair for pair, count in next_use.items() if count > 1)
    if doubled:
        raise DataError(
            f"the Enchantment Effects sheet gives one enchantment two rows of "
            f"the same next-use action: "
            f"{', '.join(f'{e} {a}' for e, a in doubled)}. The game keeps "
            f"one count of charges per enchantment and action.")

    pairs: dict[tuple, int] = {}
    for row in out:
        # THE ACTION COLUMNS ARE PART OF THE KEY, so one enchantment may both
        # restore health and generate a resource on the same event without the
        # two reading as one row written twice.
        key = (row["Enchantment"], row["Stat"], row["Condition"],
               row["ConditionValue"], row["Scale"], row["ScaleStep"],
               row["Action"], row["ActionEvent"], row["FractionOf"],
               # AND THE SECOND CONDITION, since issue #1833 gave a row one.
               row["Condition2"], row["ConditionValue2"])
        pairs[key] = pairs.get(key, 0) + 1
    twice = sorted(key for key, count in pairs.items() if count > 1)
    if twice:
        listed = ", ".join(f"{key[0]} granting {key[1]}" for key in twice)
        raise DataError(
            f"the Enchantment Effects sheet grants the same stat twice on one "
            f"enchantment, under the same condition and scale: {listed}. Two "
            f"rows for one enchantment are for two different stats, or for the "
            f"same stat under different conditions or scales; anything else is "
            f"a duplicated row.")

    # A SET IS WRITTEN WHOLE OR NOT AT ALL. A set's first bonus and its drawback
    # turn on together, at the same threshold, which is the owner's ruling of
    # 2026-09-08. So a set written here without its drawback would be a bonus
    # with no cost, and one written without its first bonus a cost with no
    # bonus. A 6-piece or 10-piece row without the first bonus is the same
    # fault: a player only ever reaches it through pieces that already meet the
    # first threshold.
    #
    # THE DRAWBACK IS THE FIRST IN SORTED ORDER, which is the row
    # `UCataclysmDropRoll::EnchantmentSetsFor` gives the set when a set has more
    # than one. No set has a second drawback row today.
    written = {row["Enchantment"] for row in out}
    for set_id in sorted(set(bonuses) | set(drawbacks)):
        ordered = sorted(bonuses.get(set_id, []))
        named = sorted(drawbacks.get(set_id, []))
        touched = sorted({name for _, name in ordered if name in written}
                         | {name for name in named if name in written})
        if not touched:
            continue

        missing = []
        if not ordered:
            missing.append("a bonus row in the Enchantments sheet")
        elif ordered[0][1] not in written:
            missing.append(f"its first bonus, {ordered[0][1]}")
        if not named:
            missing.append("a drawback row in the Enchantments sheet")
        elif named[0] not in written:
            missing.append(f"its drawback, {named[0]}")

        if missing:
            raise DataError(
                f"the Enchantment Effects sheet writes {', '.join(touched)} for "
                f"set {set_id} but not {' and '.join(missing)}. A set is "
                f"written whole or not at all: its first bonus and its drawback "
                f"turn on together at the same threshold, so half a set would "
                f"be a bonus with no cost, or a cost with no bonus.")

    if not out:
        raise DataError("the Enchantment Effects sheet is empty")
    return out


#: A range in an enchantment's sentence: two numbers joined by a hyphen, each
#: with an optional percent sign, and optional spaces around the hyphen. A
#: number may carry thousands commas and a decimal part. This is the reading
#: `UCataclysmItemValues::EnchantmentRanges` makes in the game, and
#: `tools/tests/test_enchantment_effects_match_the_row_text.py` holds the two
#: to one count of the ranges in the two enchantment tables.
_ENCHANTMENT_NUMBER = r"(?:\d[\d,]*\d|\d)(?:\.\d+)?"
ENCHANTMENT_RANGE = re.compile(
    rf"(?<![\d.,])({_ENCHANTMENT_NUMBER})%? *- *({_ENCHANTMENT_NUMBER})%?")


def enchantment_ranges(text: str) -> list[tuple[float, float]]:
    """Every range an enchantment's sentence states, as (first, second).

    "Your block chance is increased by 10%-20%" gives [(10.0, 20.0)]. A hyphen
    that does not sit between two numbers, as in "2-Piece", is not a range.
    """
    return [(float(first.replace(",", "")), float(second.replace(",", "")))
            for first, second in ENCHANTMENT_RANGE.findall(text)]


def item_base_flat_stats(item_bases: list[dict] | None) -> set[str]:
    """The stats an item base supplies as a flat implicit.

    A BASE UNDER AN INCREASE, WHICH IS THE ONLY THING THIS IS FOR. See
    `validate_passive_effects` below for the argument. `attack_damage` is the
    stat that needs it: no class stat line names it, deliberately, because a
    character's damage comes from the weapon in its hands.

    HELD HERE RATHER THAN IN THE TEST THAT ALSO NEEDS IT, so that the rule is
    stated once. `tools/tests/test_passive_effects_match_the_node_text.py`
    imports this function.

    EMPTY IS A LEGITIMATE ANSWER and not a failure: a caller that has no
    ItemBases table gets a smaller set of known stats and so a stricter check,
    which is the safe direction.
    """
    if not item_bases:
        return set()

    supplied: set[str] = set()
    for row in item_bases:
        for index in (1, 2):
            stat = str(row.get(f"Implicit{index}Stat", "") or "").strip()
            kind = str(row.get(f"Implicit{index}Kind", "") or "").strip().lower()
            if stat and kind == "flat":
                supplied.add(stat)
    return supplied


#: An item base column that supplies a stat's base, and the stat it supplies.
#:
#: A SECOND ROUTE FROM A WEAPON TO A BASE, and it is not a flat implicit. Issue
#: #1002. `attack_damage` reaches a character as an implicit on the weapon's
#: base, which `item_base_flat_stats` above sees; a swing rate is a COLUMN
#: instead, because two weapons average theirs and an implicit cannot be
#: averaged. `UCataclysmPlayerClassStats::StatBasesFromWeapons` reads that column
#: and writes the base, and the C++ test's own map of where a stat comes from
#: says exactly this: attack speed comes from "the worn weapons, as a base
#: override from StatBasesFromWeapons".
#:
#: WHY IT MATTERS TO THE CHECK BELOW. Without this, an `increased` row on
#: `attack_speed` is refused as an increase with no base under it -- and that is
#: wrong, because every weapon in the game carries one. The Masochist's Sanguine
#: Momentum node is the first to need it.
ITEM_BASE_COLUMN_STATS = {"AttackSpeed": "attack_speed"}


def item_base_column_stats(item_bases: list[dict] | None) -> set[str]:
    """The stats an item base supplies as a column rather than as an implicit.

    A COLUMN THAT IS ZERO EVERYWHERE SUPPLIES NOTHING, which is why this looks
    at the values rather than only at the header. Every armour base in the file
    carries an `AttackSpeed` of 0.0, and only the fourteen weapon bases carry a
    real one; a header alone would say a stat was supplied by a file that only
    ever writes nothing into it.
    """
    if not item_bases:
        return set()

    supplied: set[str] = set()
    for row in item_bases:
        for column, stat in ITEM_BASE_COLUMN_STATS.items():
            raw = str(row.get(column, "") or "").strip()
            try:
                if raw and float(raw) > 0.0:
                    supplied.add(stat)
            except ValueError:
                continue
    return supplied


#: Stats whose base the ENGINE supplies at the moment it asks for them, mapped to
#: where that base comes from.
#:
#: THE FOURTH KIND OF SUPPLIER, and it is named one stat at a time on purpose.
#: The other three -- a class stat line, a flat row in this sheet, a flat implicit
#: or column on an item base -- are all readable from the generated data, so the
#: check can see them. This one cannot be seen from data at all: the base is an
#: argument C++ passes to `StatForSkill`, and nothing in any CSV mentions it.
#:
#: WHY IT IS STILL A REAL SUPPLIER. The complaint this whole check exists to make
#: is that "an increase with no base under it is worth zero". A base passed by the
#: caller is a base. `attack_speed` is the precedent already accepted beside it:
#: no class line names it, and `StatBasesFromWeapons` writes it from the worn
#: weapon's column.
#:
#: WHY IT IS A DICTIONARY AND NOT A SET. Naming the source is what stops this
#: becoming a place to silence the check. An entry has to say which code supplies
#: the base, so a reader can go and look, and a stat added here without one is
#: obvious.
#:
#: AND NAMING ONE IS A PROMISE SOME CODE HAS TO KEEP. Issue #1025. The first entry
#: below named a constant nothing ever passed, so the exemption silenced this
#: check and the base really was zero: The Breaking Point opened a conversion
#: window of zero seconds and converted nothing, for as long as the node existed.
#: `UCataclysmPlayerClassStats::EngineSuppliedBases` is the map that now keeps the
#: promise, and `Cataclysm.PlayerStats.EveryEngineSuppliedBaseReachesACharacter`
#: is the test that checks it is kept.
ENGINE_SUPPLIED_BASES = {
    # Issue #985, The Breaking Point: "The conversion lasts 3 seconds, increased
    # by 5% per point". The 3 seconds is a constant of the mechanic rather than
    # anything about a character, so it is not a class stat line -- the class
    # sheet mirrors `sim/cataclysm_sim/classes.py`, which is a statement about
    # what makes each class feel different, and a timing window for one node is
    # not that.
    "damage_to_bleeding_window":
        "UCataclysmDamageConversion::BaseWindowSeconds, put on the character by "
        "UCataclysmPlayerClassStats::EngineSuppliedBases",

    # Issue #1026. What share of a hit a character takes, at 100 for normal, and
    # the same again for a hit that is damage over time. Three Masochist nodes
    # move them and all three are written as a percentage of what would otherwise
    # arrive, so a base of zero would leave every one of them worth nothing.
    #
    # NOT A CLASS STAT LINE, THOUGH FIVE STATS OF THE SAME SHAPE ARE ONE.
    # `Cataclysm.Attributes.CharacterSheetIsComplete` gives the rule: a stat is
    # off the character sheet when no affix grants it, nothing scales it, it has
    # no baseline of its own, and one passive node is its only source. The five
    # 100-means-normal stats on the `Default` line all fail that rule -- affixes
    # grant them and the Ritualist starts at 110 area of effect -- and these two
    # meet it. `docs/DECISIONS.md` records that they should be promoted to a
    # class line the day an affix grants one or a class differs on one.
    "damage_taken":
        "UCataclysmDamageCalculation::NormalDamageTaken, put on the character by "
        "UCataclysmPlayerClassStats::EngineSuppliedBases",
    "damage_over_time_taken":
        "UCataclysmDamageCalculation::NormalDamageTaken, put on the character by "
        "UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND HOW LONG A LASTING HARMFUL EFFECT ON THE CHARACTER RUNS, at 100 for
    # normal. Issue #1033. The Masochist's Symphony of Pain adds 2% a point and
    # its Vessel of Plagues adds 50%, and BOTH ARE `increased` ROWS, so without
    # a base under them the stat would resolve to zero and every stun and every
    # burn in the game would end the instant it landed. Issue #1025 records that
    # exact failure happening to the conversion window.
    "debuff_duration_taken":
        "UCataclysmDebuffs::NormalDuration, put on the character by "
        "UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND HOW LONG A STAGGER THIS CHARACTER APPLIES TO SOMEONE ELSE RUNS, at 100
    # for normal. Issue #45. The MIRROR of the entry above rather than a repeat
    # of it: that one is how long a harmful effect put ON this character runs.
    # Both scale one stagger's duration, from opposite ends.
    #
    # THE ENCHANTMENT THAT MOVES IT IS AN `increased` ROW -- "Stagger effects you
    # apply last 50%-100% longer" -- so without a base under it the stat resolves
    # to zero, `ApplyStagger` scales by zero and refuses, and no player staggers
    # anything at all. The attribute set's constructor stating 100 does not save
    # it: `ApplyTo` writes the resolved zero over that.
    "stagger_duration":
        "UCataclysmSkillEffects::NormalStaggerDuration, put on the character by "
        "UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND WHAT SHARE A HIT KEEPS WHEN ITS CRITICAL ROLL FAILS, at 100. Issue
    # #1686: "Non-critical strikes deal 20%-35% less damage" is a `more` row on
    # it, so with no base under it every non-critical hit would deal nothing.
    "non_critical_damage":
        "UCataclysmDamageCalculation::NormalNonCriticalDamage, put on the "
        "character by UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND THE SHARE A BLOCK REMOVES, at 50. Issue #1833 group E part 2, ruled
    # 2026-10-02: "You block for 65%-75% of damage instead of the normal 50%" is
    # a `flat` row of 15 to 25 on it.
    "block_damage_reduction":
        "UCataclysmDamageCalculation::BlockDamageReduction, put on the "
        "character by UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND WHAT SHARE A PROJECTILE'S LANDED CONTACTS AFTER ITS FIRST KEEP, at
    # 100. Issue #1686: "Projectiles deal 20%-35% less damage on each
    # subsequent hit after the first" is a `more` row on it.
    "projectile_later_hit_damage":
        "UCataclysmDamageCalculation::NormalProjectileLaterHitDamage, put on "
        "the character by UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND WHAT SHARE A SKILL'S ZONE DEALS ON ITS FIRST SWEEP, at 100. Issue
    # #1686: "Persistent AOE zones deal 20%-35% less damage on initial
    # placement" is a `more` row on it.
    "zone_first_sweep_damage":
        "UCataclysmDamageCalculation::NormalZoneFirstSweepDamage, put on the "
        "character by UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND WHAT SHARE OF ITS STATED TIME A GROUND ZONE OR A TERRAIN LASTS, at
    # 100. Ruled 2026-10-06: "Persistent AOE effects expire 40%-60% faster" is
    # a `more` row on it.
    "persistent_area_duration":
        "UCataclysmDamageCalculation::NormalPersistentAreaDuration, put on "
        "the character by UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND HOW FAST THE CLASS RESOURCE IS GAINED, at 100. Ruled 2026-10-07: "Your
    # class resource generates 20%-40% faster" and "... 30%-50% slower" are
    # `increased` rows on it, so without a base every gain would be nought.
    "class_resource_generation":
        "UCataclysmAbilitySystemComponent::NormalClassResourceGeneration, put "
        "on the character by UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND HOW FAST A PAYMENT OF LEECH ARRIVES, at 100. Ruled 2026-10-07: "Your
    # leech pays out 50% more slowly" (was worded "50% less tick rate for your
    # leech effects" until #1618) is a `more` row of -50 on it, so without a
    # base the rate would be nought.
    "leech_payout_rate":
        "UCataclysmLeech::NormalPayoutRate, put on the character by "
        "UCataclysmPlayerClassStats::EngineSuppliedBases",

    # AND HOW LARGE THE CRIPPLE AND WEAKEN THIS CHARACTER APPLIES ARE, at 100 for
    # normal. Issue #1767. The Ravager's Dragging Weight reads "+3% increased
    # magnitude of Cripple you apply per point" and its Sapped says the same of
    # Weaken; both are `increased` rows, so without a base under them the stat
    # resolves to zero and every Cripple and Weaken a player applies lands at
    # magnitude zero -- worse than the stat not existing at all.
    #
    # THE C++ SIDE DERIVES THESE FROM THE AILMENT TABLE rather than naming them,
    # because the names already live on the kind rows as `MagnitudeStat`. They
    # are named here because this dictionary is keyed by stat name and has to be.
    "cripple_magnitude":
        "UCataclysmAilments::NormalMagnitude, put on the character by "
        "UCataclysmPlayerClassStats::EngineSuppliedBases",
    "weaken_magnitude":
        "UCataclysmAilments::NormalMagnitude, put on the character by "
        "UCataclysmPlayerClassStats::EngineSuppliedBases",
}


#: Where the engine keeps the stats that deliberately have NO gameplay attribute.
PLAYER_CLASS_STATS_CPP = (
    REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
    / "CataclysmPlayerClassStats.cpp")

#: The shape of that accessor, so a rename fails loudly here rather than
#: silently returning an empty set and refusing every row that needs it.
_STATS_WITH_NO_ATTRIBUTE = re.compile(
    r"UCataclysmPlayerClassStats::StatsWithNoAttribute\(\)\s*\{.*?"
    r"static const TArray<FString> Stats = \{(?P<body>.*?)\};",
    re.S)


#: Where the engine keeps the enchantment row names that were renamed. Issue
#: #1799, the owner's decision of 2026-09-30.
ENCHANTMENT_RENAMES_CPP = (
    REPO_ROOT / "game" / "Source" / "Cataclysm" / "Items"
    / "CataclysmEnchantmentRenames.cpp")

#: The shape of that table, so a rename of the accessor fails loudly here.
_ENCHANTMENT_ALIASES = re.compile(
    r"FCataclysmEnchantmentRenames::Aliases\(\)\s*\{.*?"
    r"static const TMap<FName, FName> Table = \{(?P<body>.*?)\n\t\};",
    re.S)
_ALIAS_PAIR = re.compile(r'\{\s*TEXT\("([^"]+)"\)\s*,\s*TEXT\("([^"]+)"\)\s*\}')


def enchantment_aliases() -> dict[str, str]:
    """Every renamed enchantment row, old name to new, READ FROM THE ENGINE.

    One table, in `FCataclysmEnchantmentRenames::Aliases()`, which the save
    loader applies. Read here rather than restated, so the checks on it in
    `tools/tests/` look at the table the game uses.

    RAISES RATHER THAN RETURNING EMPTY when the table cannot be found or holds
    no pair, for the reason `stats_with_no_attribute` gives: an empty answer
    would make every check on it pass while reading nothing.
    """
    if not ENCHANTMENT_RENAMES_CPP.is_file():
        raise DataError(f"{ENCHANTMENT_RENAMES_CPP} is missing, so the renamed "
                        f"enchantment rows cannot be read.")
    found = _ENCHANTMENT_ALIASES.search(
        ENCHANTMENT_RENAMES_CPP.read_text(encoding="utf-8", errors="replace"))
    if not found:
        raise DataError(f"FCataclysmEnchantmentRenames::Aliases() was not found in "
                        f"{ENCHANTMENT_RENAMES_CPP.name} in its expected shape.")
    pairs = _ALIAS_PAIR.findall(found.group("body"))
    if not pairs:
        raise DataError("FCataclysmEnchantmentRenames::Aliases() parsed to no pair.")
    aliases: dict[str, str] = {}
    for old, new in pairs:
        if old in aliases:
            raise DataError(f"{old} is renamed twice in the alias table.")
        aliases[old] = new
    return aliases


def stats_with_no_attribute() -> set[str]:
    """The stats exempt from needing a gameplay attribute, READ FROM THE ENGINE.

    NOT RESTATED HERE, AND THAT IS THE WHOLE POINT. `ENGINE_SUPPLIED_BASES` above
    is a Python restatement of a C++ fact, held honest only by a test checking the
    named symbols exist. This one cannot drift, because there is only one list and
    Python reads it.

    WHY THESE STATS ARE EXEMPT. A minion's damage, health and attack interval come
    from its own row in `game/Data/MinionTypes.csv`, raised by its summoner's
    level. A summoner's gear supplies an INCREASE to apply to that figure rather
    than a value of its own, so there is nothing for an attribute to hold. An
    increase against a base of zero is zero, which is why a stat like this is
    normally a defect and why the exemption is named rather than inferred.
    Issues #898 and #1733.

    AND `mana_on_hit`, WHICH IS NOT A MINION'S. Issue #1791. The basic attack's
    mana on hit is its slot's own figure, so there is nothing for an attribute to
    hold either; a row may only remove it, which `REMOVAL_ONLY_STATS` enforces.

    AN EXEMPTION IS A PROMISE AND THIS DOES NOT KEEP IT. All this does is stop the
    sheet being refused. That every name here is really read by code is held by
    `Cataclysm.StatExemption.EveryStatWithNoAttributeIsActuallyRead`, which grants
    each stat and asserts the reading code answers differently. Issue #1025 is what
    an unkept exemption costs: one inert entry, one node converting damage for zero
    seconds, and five further nodes starved.

    RAISES RATHER THAN RETURNING EMPTY when the accessor cannot be found. An empty
    set would silently refuse every row that needs the exemption, and the error
    would name the row rather than the parse — which is the wrong place to look.
    """
    if not PLAYER_CLASS_STATS_CPP.is_file():
        raise DataError(
            f"{PLAYER_CLASS_STATS_CPP} is missing, so the stats exempt from "
            f"needing a gameplay attribute cannot be read. That file holds the "
            f"one list; this module does not keep a copy on purpose.")

    source = PLAYER_CLASS_STATS_CPP.read_text(encoding="utf-8", errors="replace")
    found = _STATS_WITH_NO_ATTRIBUTE.search(source)
    if not found:
        raise DataError(
            f"could not find UCataclysmPlayerClassStats::StatsWithNoAttribute() "
            f"in {PLAYER_CLASS_STATS_CPP.name}. If it was renamed or reshaped, "
            f"update _STATS_WITH_NO_ATTRIBUTE here rather than restating the "
            f"names, so the two cannot disagree.")

    names = set(re.findall(r'TEXT\("(\w+)"\)', found.group("body")))
    if not names:
        raise DataError(
            f"UCataclysmPlayerClassStats::StatsWithNoAttribute() was found in "
            f"{PLAYER_CLASS_STATS_CPP.name} and parsed to nothing. An empty "
            f"exemption refuses every row that needs it, so this is raised here "
            f"rather than reported later against a row.")
    return names


#: The body of `UCataclysmPlayerClassStats::StatToAttribute`, the map from a stat
#: name to the gameplay attribute that holds it. Issue #2387.
_STAT_TO_ATTRIBUTE = re.compile(
    r"UCataclysmPlayerClassStats::StatToAttribute\(\)\s*\{(?P<body>.*?)\n\}", re.S)

#: One entry of that map: the name is a literal, `TEXT("agility")`, or a named
#: constant, `UCataclysmRegeneration::ManaRegenStat` (sometimes wrapped in
#: `FString(...)`), and the value is an attribute getter.
_STAT_TO_ATTRIBUTE_ENTRY = re.compile(
    r'\{\s*(?:FString\(\s*)?(?:TEXT\("(?P<literal>\w+)"\)|(?P<constant>[A-Za-z_]\w*::\w+))'
    r'\s*\)?\s*,\s*\w+::Get\w+Attribute\(\)\s*\}')

#: A stat name defined as a constant in a source file, which is how most entries
#: of the map name their stat.
_STAT_NAME_CONSTANT = re.compile(r'const TCHAR\*\s+(\w+::\w+)\s*=\s*TEXT\("(\w+)"\)')


@functools.lru_cache(maxsize=1)
def stats_with_an_attribute() -> frozenset[str]:
    """The stats with a gameplay attribute, READ FROM THE ENGINE. Issue #2387.

    THE OTHER HALF OF WHAT THE GAME READS. A stat row reaches a character only
    through `UCataclysmPlayerClassStats::ApplyTo`, which records a stat that is in
    `StatToAttribute` or in `StatsWithNoAttribute` and drops every other name in
    silence. `stats_with_no_attribute` reads the second list; this reads the first,
    so that a flat row on a name in neither -- a misspelling, or a stat whose
    mechanism has not merged -- is refused instead of written and dead.

    THE MAP NAMES MOST OF ITS STATS BY CONSTANT, so each constant is looked up
    where it is defined (`const TCHAR* UCataclysmRegeneration::ManaRegenStat =
    TEXT("mana_regen")`) in the game's source files, tests left out.

    RAISES RATHER THAN RETURNING A PARTIAL SET, when the map cannot be found, when
    an entry does not parse, or when a constant has no definition: a partial set
    would refuse rows that work, and the message would name the row rather than
    the parse.
    """
    if not PLAYER_CLASS_STATS_CPP.is_file():
        raise DataError(
            f"{PLAYER_CLASS_STATS_CPP} is missing, so the stats with a gameplay "
            f"attribute cannot be read.")
    source = PLAYER_CLASS_STATS_CPP.read_text(encoding="utf-8", errors="replace")
    found = _STAT_TO_ATTRIBUTE.search(source)
    if not found:
        raise DataError(
            f"could not find UCataclysmPlayerClassStats::StatToAttribute() in "
            f"{PLAYER_CLASS_STATS_CPP.name}. If it was renamed or reshaped, update "
            f"_STAT_TO_ATTRIBUTE here rather than restating the names.")
    body = found.group("body")
    getters = len(re.findall(r"::Get\w+Attribute\(\)", body))
    entries = list(_STAT_TO_ATTRIBUTE_ENTRY.finditer(body))
    if not entries or len(entries) != getters:
        raise DataError(
            f"UCataclysmPlayerClassStats::StatToAttribute() holds {getters} "
            f"attribute getters and {len(entries)} entries parsed. An entry in a "
            f"shape _STAT_TO_ATTRIBUTE_ENTRY does not read would be left out, so "
            f"update the pattern rather than letting the set come up short.")

    constants: dict[str, str] = {}
    for path in (REPO_ROOT / "game" / "Source" / "Cataclysm").rglob("*.cpp"):
        if "Tests" in path.parts:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for name, value in _STAT_NAME_CONSTANT.findall(text):
            constants.setdefault(name, value)

    names: set[str] = set()
    for entry in entries:
        if entry.group("literal"):
            names.add(entry.group("literal"))
        elif entry.group("constant") in constants:
            names.add(constants[entry.group("constant")])
        else:
            raise DataError(
                f"UCataclysmPlayerClassStats::StatToAttribute() names its stat "
                f"by {entry.group('constant')}, and no source file defines it as "
                f"`const TCHAR* {entry.group('constant')} = TEXT(\"...\")`.")
    return frozenset(names)


#: The stats something asks for through the stat pipeline, which is the only
#: place a SCALED row can reach play.
#:
#: WHY A SCALED ROW NEEDS THIS AND AN ORDINARY ONE DOES NOT. A scaled bonus is
#: never folded into its gameplay attribute -- it would be stale the moment the
#: reading moved -- so it is worked out when something asks for the stat. Where
#: the consuming code reads the attribute instead, the row is discarded in
#: silence: no error, no warning, and the node grants nothing.
#: `Ritualist_capstone_200#3` scaled `max_energy_shield` that way and granted
#: nothing at all from the day it was written. Issue #1973.
#:
#: WHY THIS IS A HAND LIST AND NOT DERIVED, which is the uncomfortable part.
#: Deriving it from the engine's call sites was tried on 2026-09-17 and got THREE
#: of the eleven wrong, every one in the refusing direction: a check built on it
#: would have refused 23 shipped rows that work. Two of the three cannot be found
#: by any search of call sites -- `attack_damage` has no lookup call at all,
#: because its asker finds the stat line and runs the pipeline inline, and
#: `health_regen` is asked through a lambda that takes the stat as a parameter.
#:
#: SO THE LIST IS HELD HONEST BY MEASUREMENT INSTEAD. Every name here has a probe
#: in `Cataclysm.StatExemption.EveryStatTheDataScalesIsAskedForThroughThePipeline`
#: that grants a scaled row, moves the reading and asserts the engine's answer
#: changes, and `tools/tests/test_every_scaled_stat_has_an_asker.py` requires this
#: list and that probe table to be equal. A name added to one without the other
#: fails.
STATS_WITH_AN_ASKER = frozenset({
    # THE FINISHING OF THE DEATH CATACLYSM, SLICE C (2026-11-26): a detonation's
    # damage scaled by the other minions near the one going off (Close Packed),
    # by the summoner's own stacks (Bursting Ranks) and by the minion's age
    # (Swollen). Asked through `UCataclysmAbilitySystemComponent::
    # MultiplierForStatAgainst` on the summoner with the minion named, by
    # `UCataclysmDetonation::DamageMultiplierFor`;
    # `ProbeScaledMinionDetonationDamage` measures it.
    "minion_detonation_damage",
    # THE DEATH TREES' SLICE C6d2 (2026-11-25): a minion's attack speed scaled by
    # the summoner's own stacks, the Necromancer's Avenging Dead. Asked through
    # `UCataclysmAbilitySystemComponent::IncreasesForStat` on the summoner, with
    # the summoner's state, by `UCataclysmCommand::AttackIntervalScaleFor`;
    # `ProbeScaledMinionAttackSpeed` measures it.
    "minion_attack_speed",
    # THE DEATH TREES' SLICE C6a (2026-11-25): extra projectiles scaled by the
    # Fervour held, the Soul Collector's Soul Barrage. Asked through the
    # pipeline with the character's state by
    # `UCataclysmProjectileSkill::ExtraProjectilesWithRows`;
    # `ProbeScaledProjectileCount` measures it.
    "projectile_count",
    # THE DEATH TREES' SLICE C6b4 (2026-11-25): Poison damage scaled by the
    # seconds the creature struck has stayed poisoned by the asker, the Shadow's
    # Festering. Asked by each tick in
    # `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` with the creature
    # it lands on and with no target, the tick taking the ratio;
    # `ProbeScaledDotDamage` measures it.
    "dot_damage",
    # THE DEATH TREES' SLICE C4a (2026-11-25): Fervour a kill pays, scaled by the
    # living enemies within 8 metres for the Soul Collector's Thinning the Herd.
    # Asked through the pipeline at the kill by `UCataclysmFervour::GainOnOwnKill`;
    # `ProbeScaledFervourPerKill` measures it.
    "fervour_per_kill",
    # ADDED 2026-11-25 FOR SUPPORT NETWORK, "At 4 points: Active gadgets increase
    # trap trigger radius by 10%": `trap_trigger_radius` scaled by gadgets active,
    # one step at most. `ACataclysmMinion::SkillTrapTriggerRadiusCmFor` asks it
    # through `IncreasesForStat`, which evaluates the rows against the player's
    # state; `ProbeScaledTrapTriggerRadius` measures it.
    "trap_trigger_radius",
    # ADDED 2026-11-25 FOR WARMONGER, "While in Wrath, each enemy within melee
    # range drains 3 additional Fervour per second": `fervour_drain_per_second`
    # scaled by enemies within 4 metres. `FCataclysmBerserkerStates::DrainStep`
    # asks it with `StatForSkill`; `ProbeScaledFervourDrainPerSecond` measures it.
    "fervour_drain_per_second",
    # ADDED 2026-10-05 FOR "Starvation (10-Piece Bonus): When you kill an enemy,
    # gain a stack of 'Famished.' Each stack increases all of your leech by 5%",
    # issue #1833, three rows scaled by their own stacks. `UCataclysmLeech::NoteHit`
    # has asked all three through `StatForSkill` since issue #947;
    # `ProbeScaledLifeLeech`, `ProbeScaledManaLeech` and
    # `ProbeScaledEnergyShieldLeech` measure that with the scale the rows carry.
    "life_leech",
    "mana_leech",
    "energy_shield_leech",
    # ADDED 2026-10-02 FOR "Consecutive blocks within 3 seconds each block 5%-10%
    # more damage", issue #1833 group E part 2, a row scaled by its own stacks.
    # `UCataclysmDamageCalculation::BlockShareOf` asks it through `StatForSkill`
    # on every blocked blow; `ProbeScaledBlockDamageReduction` measures that with
    # the scale the row carries.
    "block_damage_reduction",
    # ADDED 2026-09-30 FOR THE DEMON KING'S REGALIA'S 6-PIECE, issue #1833 group D
    # part 2: "becoming immune to all crowd control ... for 10 seconds", a row
    # scaled by its own stacks. `HeldSecondsAfterCrowdControlResistance` in
    # CataclysmSkillEffects.cpp asks it through `StatForSkill` on every crowd
    # control; `ProbeScaledCrowdControlResistance` measures that.
    "crowd_control_resistance",
    # ADDED 2026-09-25 FOR "Each active gadget increases your evasion chance by
    # 5%-10%", issue #1833, deployable Part 3. `DefenderStat` in
    # CataclysmDamageCalculation.cpp asks it through `StatForSkill` on every
    # blow; `ProbeScaledEvasion` measures that with the scale the row carries.
    "evasion",
    # ADDED 2026-09-30 FOR THE TWO SKILL-LOCK NEGATIVES, issue #1833: "Killing
    # an enemy triggers a 1-2 second global cooldown on all your skills" and
    # its critical strike twin, each a row counting its own stacks.
    # `UCataclysmSkillTemplate::CanActivateAbility` asks it through
    # `StatForSkill` with the skill's tags before every activation that is not
    # a basic attack; `ProbeScaledSkillLocked` measures that with the scale the
    # rows carry.
    "skill_locked",
    # ADDED 2026-09-30 FOR "Each minion reserves 100-500 hp", issue #1833,
    # health reservation. `UCataclysmAbilitySystemComponent::HealthReserved`
    # asks it through `StatAppliedTo` with the character's state, and every
    # heal's ceiling and the regeneration step's hold read that;
    # `ProbeScaledHealthReserved` measures it with the scale the row carries.
    "health_reserved",
    # ADDED 2026-09-25 FOR "You lose 1-4% max resistances for every 100,000 -
    # 500,000 kills", issue #1833, the kill counter.
    # `UCataclysmDamageCalculation::ResistanceCapOf` asks it through
    # `StatAppliedTo` with 70 as the base, on every blow and on the sheet;
    # `ProbeScaledResistanceCap` measures that with the scale the row carries.
    "resistance_cap",
    "attack_damage",
    "spell_damage",
    "attack_speed",
    "armor",
    "damage_reduction",
    # ADDED 2026-09-23 FOR THE TWO "IN COMBAT" DAMAGE TAKEN ROWS, issue #1815.
    # `DefenderStat` in CataclysmDamageCalculation.cpp asks it through
    # `StatForSkill` on every blow, which is how its conditioned rows already
    # work; no scaled row had asked for it until now, so no probe did either.
    "damage_taken",
    "retaliation",
    "health_regen",
    "mana_regen",
    "fervour_per_second",
    "fervour_per_enemy_in_reach",
    "fervour_from_minions",
    "max_energy_shield",
    # ADDED 2026-09-18 FOR Ritualist_keystone_d_kC Vessel, and only
    # because a probe measures the ask.
    # UCataclysmAbilitySystemComponent::MaximumClassResource applies a
    # scaled row to the maximum, thirteen of the fourteen readers of that
    # maximum now go through it, and
    # Cataclysm.StatExemption.EveryStatTheDataScalesIsAskedForThroughThePipeline
    # moves maximum mana and watches the bar move with it. Before that
    # lookup existed every reader took the attribute, and a scaled row on
    # this stat would have been accepted, built, imported and dead.
    "class_resource",
    # ADDED 2026-09-23 FOR "Each unique debuff on an enemy increases your crit
    # chance against them by 5%-10%", issue #1815. The critical strike site in
    # CataclysmVitalAttributeSet.cpp asks it through `StatForSkill` on every
    # blow; `ProbeScaledCritChance` measures that.
    "crit_chance",
    # ADDED 2026-09-23 FOR "Each active minion reduces your maximum HP by
    # 3%-6%", issue #1815. `RefreshLiveMaximumHealth` asks the whole line with
    # the character's state and writes the attribute, from `ApplyTo` and from
    # each regeneration step; `ProbeScaledMaximumHealth` measures that.
    "max_health",
    # ADDED 2026-09-24 FOR "Each skill use increases your movement speed by
    # 3%-5% for 2 seconds, stacking up to 5 times", issue #1833.
    # `ACataclysmPlayerCharacter::RefreshMovementSpeed` asks it through
    # `StatForSkill`, and `MovementSpeedCanChangeUnannounced` asks again on
    # the quarter-second step for any scaled row; `ProbeScaledMovementSpeed`
    # measures that on a spawned player character.
    "movement_speed",
    # ADDED 2026-10-07 FOR THE THREE TRAP ROWS THAT SCALE BY A COUNT, issue
    # #2284: "for each other trap currently active", "Each active gadget
    # increases trap damage" and "Each gadget on the battlefield doubles the
    # damage of all your traps". A machine's swing has asked this stat through
    # the pipeline since it was written, in `ACataclysmMinion::AttackTarget` by
    # `UCataclysmCommand::SummonerMultiplierAgainst`, with the summoner's
    # state; the list is brought up to the code. `ProbeScaledMinionDamage`
    # measures it on a trap's blow with the scale and the offset the first of
    # those rows carries.
    "minion_damage",
    # ADDED 2026-11-24 FOR BOUND SERVANTS, the Ritualist's First Pact's first
    # option in the demonic tree rework the owner approved that day: "Your
    # minions deal 5% increased damage and have 5% increased health for each
    # minion you have", two rows scaled by `minions_held`. A minion's health is
    # asked when it is made, in `ACataclysmMinion::Spawn`, through
    # `UCataclysmCommand::SummonerMultiplierFor`, which runs the summoner's line
    # through the pipeline with `CurrentConditions` -- so the scaled row is read
    # with the summoner's count, the minion being made included. The list is
    # brought up to the code; `ProbeScaledMinionHealth` measures it.
    "minion_health",
    # ADDED 2026-10-17 FOR SPELLSLINGER, Spellblade's Will's six-piece bonus,
    # "makes your spells cost 10% less mana per stack", a row scaled by
    # its own stacks; ruled that day (R1). `UCataclysmGameplayAbility::ManaCostFor`
    # asks it through `StatAppliedTo` with the skill's tags for every cost the
    # game reads; `ProbeScaledManaCost` measures that with the scale the row
    # carries.
    "mana_cost",
    # ADDED 2026-10-18 FOR THE DEMON KING'S REGALIA'S TEN-PIECE, "instead gain
    # 100% crit chance and crit multiplier ... for 3 seconds", a row scaled by
    # its own stacks on `lethal_hit_reprieved`. The critical strike site in
    # CataclysmVitalAttributeSet.cpp asks it through `StatForSkill` on every
    # blow, beside `crit_chance`; `ProbeScaledCritMultiplier` measures that.
    "crit_multiplier",
    # ADDED 2026-10-18 FOR THE SAME SENTENCE, "If you would be killed during
    # Demonic Rage": the reprieve's interval, a row scaled by its own stacks on
    # the Rage's event and length, so it is held only during the Rage.
    # `UCataclysmAbilitySystemComponent::LethalHitReprieveFigure` asks it through
    # `StatForSkill` when a blow or a tick would kill;
    # `ProbeScaledLethalHitReprieveEvery` measures that.
    "lethal_hit_reprieve_every_seconds",
    # ADDED 2026-11-24 FOR THE MASOCHIST TREE REWORK THE PROJECT OWNER APPROVED
    # THAT DAY, two vow clauses scaled by the Fervour held. Reprisal Wave's
    # "and 1 metre further for every 25 Fervour you hold": `retaliation_radius_metres`
    # has been asked through `StatForSkill` by `UCataclysmRetaliation::RadiusMetresFor`
    # since issue #1047, and `ProbeScaledRetaliationRadius` measures it with the
    # scale the row carries. Carnivore's "each stack lasts 1 second longer for
    # every 10 Fervour you hold": `carnage_stack_seconds` is asked by
    # `UCataclysmStacks::CarnageExtraSeconds` when a Carnage stack is granted, and
    # `ProbeCarnageStackSeconds` measures it.
    "retaliation_radius_metres",
    "carnage_stack_seconds",
    # ADDED 2026-11-24 FOR THE RAVAGER TREE REWORK, three rows scaled by the
    # enemies within 4 metres. `area_of_effect` for Reach, "+1% increased Area
    # of Effect per point for each enemy within 4 metres":
    # `UCataclysmSkillEffects::AsMultiplierForSkill` has asked it through
    # `StatForSkill` with the skill's tags since issue #943, so nothing was
    # built; `ProbeScaledAreaOfEffect` measures the scaled row.
    # `health_restored_on_kill_at_no_cost` for Long Hold, "Killing an enemy
    # restores 1% of your maximum health for each enemy within 4 metres of
    # you": `UCataclysmFervour::RestoreHealthOnKillAtNoCost` asks it through
    # `StatForSkill`; `ProbeScaledHealthRestoredOnKill` measures it.
    # `ultimate_fervour_cost_reduction` for Thronged, "Your Ultimate costs 5
    # less Fervour for each enemy within 4 metres of you when you use it":
    # `UCataclysmGameplayAbility::FervourCostFor` asks it, built for it;
    # `ProbeScaledUltimateFervourCostReduction` measures it.
    "area_of_effect",
    "health_restored_on_kill_at_no_cost",
    "ultimate_fervour_cost_reduction",
    # THE SOUL COLLECTOR'S SIPHON, the Death trees' slice C2 (2026-11-25). Its
    # radius (Pall of Souls scales it by the Fervour held), Life Siphon's health
    # and Living Harvest's Fervour (each scaled by the enemies in the Siphon):
    # `FCataclysmSiphon` asks each through `StatForSkill` with the holder's
    # state; `ProbeScaledSiphonRadius`, `ProbeScaledSiphonLife` and
    # `ProbeScaledSiphonFervour` measure them.
    "siphon_radius_metres",
    "siphon_life_percent",
    "siphon_fervour_per_second",
    # THE DEATH LINE'S FINISHING SLICE E (2026-11-26): Flurry of the Dead's extra
    # strikes, 1 for every 10 Fervour held. `UCataclysmShadowImages::StrikeAgain`
    # asks it through `StatForSkill` after each landed hit; `ProbeHitsStrikeAgain`
    # measures the scaled row.
    "hits_strike_again",
})


def refuse_a_scale_nothing_asks_for(sheet: str, rows: list[dict]) -> list[str]:
    """A row may only carry a Scale on a stat something asks for.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE, which is the whole reason this
    exists: the generator writes it, the table builds, the asset imports, and the
    player gets nothing. Refusing it here is the only moment anybody is looking.
    """
    problems = []
    for row in rows:
        scale = str(row.get("Scale") or "").strip()
        if not scale:
            continue
        stat = row["Stat"]
        if stat not in STATS_WITH_AN_ASKER:
            problems.append(
                f"{sheet}/{row['Name']}: scales {stat!r} by {scale!r}, and "
                f"nothing asks for that stat through the stat pipeline. A "
                f"scaled row is never folded into its gameplay attribute, so "
                f"this row would grant NOTHING and say nothing -- which is what "
                f"Ritualist_capstone_200#3 did until issue #1973. Give the stat "
                f"a lookup where its value is used, and add it to "
                f"STATS_WITH_AN_ASKER here and to the probe table in "
                f"CataclysmStatExemptionTests.cpp; or take the scale off.")
        elif (scale.lower() in SCALES_THAT_COUNT_A_FRACTION
              and stat not in STATS_ASKED_WITH_A_CHARGES_DISTANCE):
            # AND A SCALE THAT READS THE BLOW NEEDS AN ASKER THAT HANDS THE
            # BLOW OVER. Ruled 2026-10-08. The stat is asked for, so the check
            # above passes it, and the row would still be dead.
            problems.append(
                f"{sheet}/{row['Name']}: scales {stat!r} by {scale!r}, which "
                f"reads how far a charge had gone when its blow landed, and "
                f"the code that asks for {stat!r} is not handed that. The "
                f"scale would read nought every time, so this row would grant "
                f"NOTHING and say nothing. Only "
                f"{', '.join(sorted(STATS_ASKED_WITH_A_CHARGES_DISTANCE))} are "
                f"asked for with a charge's distance "
                f"(STATS_ASKED_WITH_A_CHARGES_DISTANCE).")
    return problems


#: The conditions that ask about A BLOW BEING TAKEN: what kind it is and who
#: dealt it. Only a stat asked for at a defence step is handed that blow
#: (`DefenderStat` in `CataclysmDamageCalculation.cpp`), so on any other stat a
#: row under one of these is refused every time it is judged.
CONDITIONS_OF_A_BLOW_TAKEN = frozenset({
    "attacker_beyond_metres",
    "hit_is_melee_attack",
    "hit_is_ranged_attack",
    "hit_is_spell",
    "melee_hit_while_moving",
    "opponent_carries_weaken",
    "opponent_first_struck_by_you_seconds_ago",
    "opponent_is_boss",
    # AND READ THE BLOW'S ATTACKER RUNG, the Death trees' slice C6b3 (2026-11-25).
    "opponent_rarity_at_least",
    "opponent_is_crowd_controlled",
    "opponent_is_staggered",
    "opponent_within_metres",
})

#: The conditions that ask about A HIT BEING DEALT: the character struck, how
#: many were struck together, and what the skill cost or how far its user moved
#: before it. Only a stat asked for while a hit is priced is handed those.
CONDITIONS_OF_A_HIT_DEALT = frozenset({
    "enemies_hit_at_least",
    "enemies_hit_at_most",
    "metres_moved_before_attack",
    "skill_health_cost_above",
    "target_carries_a_dot",
    "target_carries_any_debuff",
    "target_carries_cripple",
    "target_carries_cripple_and_weaken",
    "target_carries_void_splinter",
    "target_damaged_by_you_within_seconds",
    "target_near_your_kill_within_seconds",
    "target_struck_from_behind",
    "target_at_full_health",
    "target_carries_your_poison",
    "target_beyond_metres",
    "target_struck_by_your_movement_skill_within_seconds",
    "target_carries_necrosis",
    "target_marked_by_you",
    "target_is_your_last_attacker",
    "target_health_below",
    "target_is_boss",
    "target_is_not_boss",
    "target_in_your_zone",
    "target_in_your_siphon",
    "target_is_staggered",
    "target_not_yet_crit_by_you",
    "target_not_yet_struck_by_you",
    "target_within_metres",
    "target_is_silenced",
    "target_is_moving",
    "target_carries_your_curse",
    "target_carries_your_poison_or_bleed",
    "target_rarity_at_least",
    "target_rarity_is",
    # The Saboteur's device conditions, 2026-11-25: Ambush. A fact of the target
    # in hand, read by `WithTargetState`; the trap rows that carry it are priced
    # by `TrapDamageNamingTrap` with the enemy struck.
    "target_not_yet_triggered_your_trap",
})

#: What a stat's asker hands the pipeline BEYOND the wearer's own state, for
#: every stat a row may state under a condition.
#:
#: WHY A CONDITIONED ROW NEEDS THIS. `UCataclysmPlayerClassStats::ApplyTo` works
#: out a gameplay attribute with an empty state, which refuses every condition.
#: So a conditioned row reaches play ONLY where the consuming code asks the stat
#: pipeline at the moment of use. Where it reads the attribute, the row is
#: accepted, built, imported and dead. Found on 2026-10-05 writing Brute's
#: Heart's six-piece bonus: a blow reads a defender's resistance from the
#: attribute, so "resistances increased by 50% below half health" would have
#: granted nothing. Issue #1833.
#:
#: AND WHY IT IS PER STAT AND NOT A FLAT LIST. A stat can be asked for and still
#: be unable to judge a condition: `movement_speed` is asked with the wearer's
#: state and no target, so a row on it under `target_is_boss` is as dead as one
#: on a stat nothing asks for. Each stat is listed with what its asker passes:
#: nothing more than the wearer's state, the blow being taken, or the hit being
#: dealt. A condition outside those two sets asks only the wearer's state, which
#: every asker has.
#:
#: A HAND LIST HELD BY MEASUREMENT, for the reason `STATS_WITH_AN_ASKER` gives:
#: deriving it from call sites was tried and got three of eleven wrong.
#: `tools/tests/test_every_conditioned_stat_has_an_asker.py` requires every name
#: here to have a probe in `CataclysmStatExemptionTests.cpp`, or to be named there
#: as read from the code and not yet probed.
#:
#: WHAT IT DOES NOT SAY. That every consumer of a stat asks. One was known to
#: have one that read the attribute, with an issue: damage priced outside a
#: hit for `attack_damage` (#2234). All three of its paths are built: the self
#: hit ("hits you instead", "backfire") reads the whole line since 2026-10-24
#: (path B), the Echo Chamber copy that hits the player since 2026-10-25 (path
#: C), and a skill's ground patch since 2026-10-25 (path A). A second, a
#: projectile skill's throw interval for `attack_speed`, asks since issue
#: #2233 (2026-10-23), once when a rack starts. A third, `dot_damage` judged once at application, is BUILT since
#: #2235 (2026-10-23): a tick priced from its applier's stats asks `dot_damage`
#: again with the character it lands on, so a condition about the target is
#: judged on each tick. A condition about the wearer's own state is still
#: judged once, when the effect is applied.
ASKER_PASSES_NOTHING_MORE: frozenset[str] = frozenset()
ASKER_PASSES_A_BLOW_TAKEN = frozenset({"blow_taken"})
ASKER_PASSES_A_HIT_DEALT = frozenset({"hit_dealt"})
CONDITIONED_STATS_WITH_AN_ASKER: dict[str, frozenset[str]] = {
    # Asked for at a defence step, with the blow being taken.
    "armor": ASKER_PASSES_A_BLOW_TAKEN,
    "block_chance": ASKER_PASSES_A_BLOW_TAKEN,
    "damage_reduction": ASKER_PASSES_A_BLOW_TAKEN,
    "damage_taken": ASKER_PASSES_A_BLOW_TAKEN,
    "evasion": ASKER_PASSES_A_BLOW_TAKEN,
    # Ever Moving's share of evasion against area damage, the Death line's finishing
    # slice E (2026-11-26): asked at the evasion step with the blow, as evasion is.
    "evasion_against_area_percent": ASKER_PASSES_A_BLOW_TAKEN,
    # Asked for while a hit is priced, with the character struck.
    "armor_penetration": ASKER_PASSES_A_HIT_DEALT,
    "attack_damage": ASKER_PASSES_A_HIT_DEALT,
    "crit_chance": ASKER_PASSES_A_HIT_DEALT,
    "crit_multiplier": ASKER_PASSES_A_HIT_DEALT,
    "minion_damage": ASKER_PASSES_A_HIT_DEALT,
    "penetration": ASKER_PASSES_A_HIT_DEALT,
    "spell_damage": ASKER_PASSES_A_HIT_DEALT,
    # THE DEATH CLASSES' KILL GENERATOR, 2026-11-24. Fervour a kill pays and the
    # decay's grace are asked at the kill with the slain creature as the target
    # (`UCataclysmFervour::GainOnOwnKill` and `NoteOwnKill`), so a rarity row is
    # judged; the decay's rate with the wearer's state alone
    # (`UCataclysmFervour::KillDecayStep`).
    "fervour_per_kill": ASKER_PASSES_A_HIT_DEALT,
    "fervour_kill_decay_grace_seconds": ASKER_PASSES_A_HIT_DEALT,
    # AND THE FERVOUR A LANDED CRITICAL STRIKE PAYS, asked with the character
    # struck as the target (`UCataclysmFervour::GainOnCriticalStrike`), so
    # Telling Blows' rarity row is judged. The Death trees' slice C4d1.
    "fervour_per_critical_strike": ASKER_PASSES_A_HIT_DEALT,
    # AND PATIENT BLADE'S AND DEATH BY INCHES', each asked with the Boss as the
    # target (`UCataclysmFervour::PayForABossTick` and `CountAHitOnABoss`), so
    # their `target_is_boss` is judged. The Death trees' slice C4d2.
    "boss_ticks_pay_a_kill_every_seconds": ASKER_PASSES_A_HIT_DEALT,
    "boss_hits_counted_as_a_kill_every": ASKER_PASSES_A_HIT_DEALT,
    "fervour_kill_decay_per_second": ASKER_PASSES_NOTHING_MORE,
    # Asked for with the wearer's own state and nothing else.
    "area_of_effect": ASKER_PASSES_NOTHING_MORE,
    # AND MAXIMUM HEALTH, the Death trees' slice C4d3 (2026-11-25), for Wearer of
    # the Dead's Titanic Resolve. `RefreshLiveMaximumHealth` asks the whole line
    # with the character's state and writes the attribute, from `ApplyTo`, from
    # each regeneration step for a line whose rows move with state (a condition
    # included), and when a modifier is worn; `ProbeConditionedMaxHealth`
    # measures that.
    "max_health": ASKER_PASSES_NOTHING_MORE,
    # The Death trees' slice C6d1 (2026-11-25): Ringed in Bone. Read by UCataclysmSkillTemplate::IsImmuneTo.
    "immune_to_slow": ASKER_PASSES_NOTHING_MORE,
    # THE DEATH TREES' SLICE C3d (2026-11-25), each for a Soul Collector vow's
    # clause under `fervour_threshold_reached`: the extra projectile, asked by
    # `UCataclysmProjectileSkill::ExtraProjectilesWithRows` with the skill's tags,
    # and the faster cooldown recovery, asked on the regeneration step by
    # `UCataclysmAbilitySystemComponent::FervourThresholdStep`. Probed by
    # `ProbeConditionedProjectileCount` and `ProbeConditionedCooldownsRecoverFaster`.
    "projectile_count": ASKER_PASSES_NOTHING_MORE,
    # AND THE SKILL'S RANGE, the Death trees' slice C4a, for the Shadow's Hunting
    # Leap under `seconds_after_kill`: asked as increases with the skill's tags by
    # `UCataclysmAbilitySystemComponent::IncreasesForStat`, with the wearer's state.
    # Probed by `ProbeConditionedSkillRange`.
    "skill_range": ASKER_PASSES_NOTHING_MORE,
    # AND THE RATE EVERY GAIN OF THE CLASS RESOURCE IS SCALED BY, the Death trees'
    # slice C4b1, for the Soul Collector's Any Fuel under
    # `fervour_gain_not_from_a_kill`: asked with no tags by
    # `UCataclysmAbilitySystemComponent::ClassResourceGainScaled`, with the
    # wearer's state. Probed by `ProbeConditionedClassResourceGeneration`.
    "class_resource_generation": ASKER_PASSES_NOTHING_MORE,
    "cooldowns_recover_faster_percent": ASKER_PASSES_NOTHING_MORE,
    "attack_speed": ASKER_PASSES_NOTHING_MORE,
    # THE BERSERKER'S STATES, 2026-11-25. The two flags `FCataclysmBerserkerStates::Judge`
    # asks with no tags, each stated under its weapon rule; the drain
    # `DrainStep` asks with no tags, stated under a state; and the Fervour per
    # blow `FCataclysmWarFervour::GainOnHitDealt` asks with the blow's skill tags
    # and no blow record. Each is probed in `CataclysmStatExemptionTests.cpp`'s
    # table of stats with no attribute.
    # THE SABOTEUR'S DEVICE CONDITIONS, 2026-11-25. Overwhelming Presence's
    # "trap trigger radius is doubled", stated under `no_room_for_a_trap`.
    # `ACataclysmMinion::SkillTrapTriggerRadiusCmFor` asks it with the trap's
    # tags and no blow; probed in the table of conditioned stats.
    "trap_trigger_radius": ASKER_PASSES_NOTHING_MORE,
    # SKULL SPLITTER, 2026-11-25: its percent a stack and its cost, each stated
    # under `in_wrath` and `wielding_two_handed_weapon`, asked by
    # `FCataclysmWarFervour::SkullSplitterOnCriticalStrike` with the crit's skill
    # tags and no blow; probed in the table of stats with no attribute.
    "crit_armor_removed_in_wrath": ASKER_PASSES_NOTHING_MORE,
    "crit_armor_fervour_cost": ASKER_PASSES_NOTHING_MORE,
    "wrath_on_full_fervour": ASKER_PASSES_NOTHING_MORE,
    "frenzy_on_full_fervour": ASKER_PASSES_NOTHING_MORE,
    "fervour_drain_per_second": ASKER_PASSES_NOTHING_MORE,
    "fervour_per_hit_dealt": ASKER_PASSES_NOTHING_MORE,
    # AND ONE OF SLICE C'S RATES, asked by `FCataclysmWarFervour::GainByRate`
    # with the event's tags and the wearer's state, for the War riders of
    # 2026-11-25: Resolve Under Fire's "below 30% HP" and Immovable's "while
    # stationary". Probed in the same table by `ProbeWarFervourGain`. ITS
    # SIBLING `fervour_per_critical_strike` (Razor Mind's "while above 75%
    # Fervour") is the Death line's since the bundle 3 merge, asked with the
    # blow by `UCataclysmFervour::GainOnCriticalStrike`: listed above.
    "fervour_per_hit_taken": ASKER_PASSES_NOTHING_MORE,
    # THE FERVOUR SPENDS, 2026-11-25. Iron Will's cost, Undying's restore and
    # Cleave's cost and gain, each asked by `FCataclysmWarFervour` with the
    # wearer's state (Cleave's with the attack's skill tags) and probed in the
    # table of stats with no attribute; and three stats the engine already
    # asked with the live state, which a War row now states under a condition:
    # `strike_arc_at_least_degrees` (`UCataclysmStrikeSkill::ArcAtLeastDegrees`,
    # Cleave under "Two-handed weapons only"), `max_health`
    # (`RefreshLiveMaximumHealth`, Blood Tithe above 50% Fervour) and
    # `block_negation_chance` (`UCataclysmDamageCalculation`'s block step, with
    # the blow being taken; Iron Will above 75 Fervour). Each has a probe in one
    # of the three tables already.
    "fervour_cost_per_negated_block": ASKER_PASSES_NOTHING_MORE,
    "health_percent_per_ten_fervour_on_lethal_blow": ASKER_PASSES_NOTHING_MORE,
    "fervour_cost_per_extra_enemy_hit": ASKER_PASSES_NOTHING_MORE,
    "fervour_per_extra_enemy_hit_paid": ASKER_PASSES_NOTHING_MORE,
    "strike_arc_at_least_degrees": ASKER_PASSES_NOTHING_MORE,
    # (`max_health` is listed above, with its probe.)
    "block_negation_chance": ASKER_PASSES_A_BLOW_TAKEN,
    # RIPOSTE, 2026-11-25: its share under at least 25 Fervour and its stun
    # under the pool at its maximum, each asked by
    # `FCataclysmWarFervour::RiposteOnBlock` with the wearer's state and no
    # blow, and probed in the table of stats with no attribute.
    "riposte_share_of_blocked": ASKER_PASSES_NOTHING_MORE,
    "riposte_stun_seconds_at_maximum": ASKER_PASSES_NOTHING_MORE,
    # ASKED WHERE A PLAYER'S STEP IS REFUSED, with no tags and no blow, by
    # `UCataclysmSkillEffects::CannotWalkByARow`. Ruled 2026-10-08, for "You
    # cannot move while channeling any skill". Its probe is `ProbeCannotWalk`,
    # which observes that function and not the step.
    "cannot_walk": ASKER_PASSES_NOTHING_MORE,
    "cooldown_reduction": ASKER_PASSES_NOTHING_MORE,
    "cooldown_skip_chance": ASKER_PASSES_NOTHING_MORE,
    "crowd_control_resistance": ASKER_PASSES_NOTHING_MORE,
    # ASKED WHEN A TICK ARRIVES, WITH THE WEARER'S STATE AND NO BLOW RECORD: a tick
    # is not a hit, so no condition of a blow taken can be judged on it. Probed
    # under `while_moving`, 2026-10-07.
    "damage_over_time_taken": ASKER_PASSES_NOTHING_MORE,
    "debuffs_do_not_expire": ASKER_PASSES_NOTHING_MORE,
    # ASKED TWICE, AND SINCE 2026-10-23 WITH THE CHARACTER STRUCK. When an effect
    # is applied, `DamageOverTimeNumbers` asks with the wearer's state and no
    # target. On each tick of an effect so priced,
    # `UCataclysmVitalAttributeSet::PostGameplayEffectExecute` asks it again with
    # the character the tick lands on, its distance and its stagger, and applies
    # what the target's conditions add (#2235, Q2 (a)). So a condition about the
    # target is judged on the tick. The four conditions of a hit dealt that are
    # about a BLOW rather than its target are handed over at neither, and
    # `CONDITIONS_OF_A_HIT_DEALT_AN_ASKER_DOES_NOT_PASS` refuses them here.
    "dot_damage": ASKER_PASSES_A_HIT_DEALT,
    "energy_shield_leech": ASKER_PASSES_NOTHING_MORE,
    "fervour_loss_suppressed": ASKER_PASSES_NOTHING_MORE,
    "fervour_per_cast": ASKER_PASSES_NOTHING_MORE,
    "fervour_per_second": ASKER_PASSES_NOTHING_MORE,
    "health_cost_suppressed": ASKER_PASSES_NOTHING_MORE,
    "health_regen": ASKER_PASSES_NOTHING_MORE,
    "knockback_suppressed": ASKER_PASSES_NOTHING_MORE,
    # THE WAR VOW OPTIONS' SECOND SLICE, 2026-11-25. Unstoppable's "cannot be
    # rooted", a pin counted as the root, under "above 75% Fervour" and
    # "Two-handed weapons only": `UCataclysmSkillEffects::ApplyPin` asks it of
    # the target with no tags; probed in the table of stats with no attribute.
    # And Ragnarok's "Your maximum Fervour is doubled", under "Two-handed weapons
    # only": `UCataclysmAbilitySystemComponent::MaximumClassResource` evaluates
    # the line with the wearer's state, which every gain, spend and full-pool
    # judgement reads; probed by `ProbeScaledMaximumClassResource` in the table
    # of scaled stats, which measures that the line is asked with the state.
    "pin_suppressed": ASKER_PASSES_NOTHING_MORE,
    "class_resource": ASKER_PASSES_NOTHING_MORE,
    "life_leech": ASKER_PASSES_NOTHING_MORE,
    "mana_cost": ASKER_PASSES_NOTHING_MORE,
    "mana_leech": ASKER_PASSES_NOTHING_MORE,
    "mana_pool_becomes_health": ASKER_PASSES_NOTHING_MORE,
    "movement_speed": ASKER_PASSES_NOTHING_MORE,
    "movement_speed_reduction_suppressed": ASKER_PASSES_NOTHING_MORE,
    "nova_damage_of_missing_health": ASKER_PASSES_NOTHING_MORE,
    # ADDED 2026-11-24 FOR THE MASOCHIST TREE REWORK. Unstable Aura's share of
    # weapon damage, under `health_at_or_below` 50, asked by `UCataclysmNova::Step`
    # with the wearer's state and no tags (its probe is in `Probes`). And
    # Threadbare's "Fervour removed by your health regeneration is reduced",
    # under the same condition: `UCataclysmFervour::RateFor` asks the removal
    # rate through `StatForSkill` with the healing's tags on every heal
    # (`ProbeConditionedFervourLostToHealing`).
    "nova_damage_of_weapon_damage": ASKER_PASSES_NOTHING_MORE,
    "fervour_lost_to_healing": ASKER_PASSES_NOTHING_MORE,
    "retaliation": ASKER_PASSES_NOTHING_MORE,
    "skill_charges_bonus": ASKER_PASSES_NOTHING_MORE,
    "skill_cost_paid_from_health": ASKER_PASSES_NOTHING_MORE,
    "skill_locked": ASKER_PASSES_NOTHING_MORE,
    # THE EIGHT RESISTANCES, ASKED WHERE A BLOW READS ONE, with the wearer's own
    # state and no blow: `UCataclysmDamageCalculation::TypedResistanceOf` adds to
    # the attribute what a conditioned row makes now, through
    # `AttributeWithWhatWasNotFolded`, which asks `CurrentConditions()` with no
    # blow in hand. Ruled 2026-10-18, for Brute's Heart's six-piece. Probed in
    # `ConditionedProbes` by `ProbeConditionedResistance`.
    "resistance_war": ASKER_PASSES_NOTHING_MORE,
    "resistance_demonic": ASKER_PASSES_NOTHING_MORE,
    "resistance_death": ASKER_PASSES_NOTHING_MORE,
    "resistance_pestilence": ASKER_PASSES_NOTHING_MORE,
    "resistance_famine": ASKER_PASSES_NOTHING_MORE,
    "resistance_celestial": ASKER_PASSES_NOTHING_MORE,
    "resistance_chaos": ASKER_PASSES_NOTHING_MORE,
    "resistance_void": ASKER_PASSES_NOTHING_MORE,
}

#: Conditions of a hit dealt that a stat's asker does NOT hand over although it
#: hands over the character struck. Ruled 2026-10-23 (#2235): `dot_damage` is
#: asked on a tick with the target, its distance and its stagger, and a tick is
#: no blow, so how many enemies a blow struck together, how far its user moved
#: before it and what its skill cost are never known there.
#: `refuse_a_condition_nothing_asks_for` refuses each of these on its stat, which
#: is what it refused before `dot_damage` was given the target.
CONDITIONS_OF_A_HIT_DEALT_AN_ASKER_DOES_NOT_PASS: dict[str, frozenset[str]] = {
    "dot_damage": frozenset({
        "enemies_hit_at_least",
        "enemies_hit_at_most",
        "metres_moved_before_attack",
        "skill_health_cost_above",
    }),
    # THE DEATH CLASSES' KILL GENERATOR, 2026-11-24: asked at a kill with the
    # slain creature and nothing else (`UCataclysmFervour::GainOnOwnKill` and
    # `NoteOwnKill`). A kill is no blow being priced, so the blow's own facts --
    # how many it struck, how far its user moved, what its skill cost, the
    # target's distance and its stagger -- are never handed over there.
    **{stat: frozenset({
        "enemies_hit_at_least",
        "enemies_hit_at_most",
        "metres_moved_before_attack",
        "skill_health_cost_above",
        "target_within_metres",
        "target_is_staggered",
    }) for stat in ("fervour_per_kill", "fervour_kill_decay_grace_seconds",
                    # AND THE FERVOUR A CRITICAL STRIKE PAYS, asked with the
                    # character struck and nothing else (slice C4d1).
                    "fervour_per_critical_strike",
                    # AND PATIENT BLADE'S AND DEATH BY INCHES', asked with the
                    # Boss and nothing else (slice C4d2).
                    "boss_ticks_pay_a_kill_every_seconds",
                    "boss_hits_counted_as_a_kill_every")},
}


#: The conditions judged from AN ACTION ROW'S EVENT and from nothing a stat
#: lookup is handed. Ruled 2026-10-17. No stat's asker passes "action_event",
#: so `refuse_a_condition_nothing_asks_for` refuses each on every stat row.
CONDITIONS_OF_AN_ACTIONS_EVENT = frozenset({
    "hit_dealt_is_not_spell",
})

#: The events whose tags are the skill of a hit the wearer dealt, which is what
#: `CONDITIONS_OF_AN_ACTIONS_EVENT` reads: each is raised in
#: `ACataclysmPlayerCharacter::OnSomethingWasHit` with the blow's skill tags.
EVENTS_CARRYING_A_HIT_DEALTS_TAGS = (
    "hit_dealt",
    "first_hit_dealt",
    "critical_strike",
)


def what_a_condition_needs(condition: str) -> str:
    """What an asker must pass for this condition to be judged: "blow_taken",
    "hit_dealt", "action_event", or an empty string for the wearer's own
    state."""
    if condition in CONDITIONS_OF_A_BLOW_TAKEN:
        return "blow_taken"
    if condition in CONDITIONS_OF_A_HIT_DEALT:
        return "hit_dealt"
    if condition in CONDITIONS_OF_AN_ACTIONS_EVENT:
        return "action_event"
    return ""


#: Conditions judged at their row's event rather than when a stat is read, each
#: with the one event whose raising fills what it reads. The Death trees' slice
#: C6d2 (2026-11-25): Avenging Dead's share of maximum health a hit took, held
#: while `hit_taken` is raised. `UCataclysmStatPipeline::IsJudgedAtItsEvent`
#: names the same. Allowed only as the Condition of an action row, or of a row
#: scaled by its own stacks, on that event, where the game judges it at the
#: grant; anywhere else it reads -1 and refuses every time.
EVENT_CONDITIONS = {"hit_took_at_least_percent_of_max_health": "hit_taken"}


def refuse_a_condition_nothing_asks_for(sheet: str, rows: list[dict]) -> list[str]:
    """A row may only carry a Condition on a stat whose asker can judge it.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE, the argument
    `refuse_a_scale_nothing_asks_for` makes for a scale. A row with no stat is
    an action, whose condition is judged where the action fires, and is skipped.
    Both of a row's conditions are checked.

    AN EVENT CONDITION (`EVENT_CONDITIONS`) is checked first, on every row: it
    is allowed only as the Condition of an action row or of an own-stack row on
    its event, and it asks nothing of the stat's asker.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        event_ok = True
        for column in ("Condition", "Condition2"):
            condition = str(row.get(column) or "").strip()
            if condition not in EVENT_CONDITIONS:
                continue
            event = str(row.get("ActionEvent") or "").strip()
            granted = not stat or str(row.get("Scale") or "").strip() == "own_stacks"
            if column != "Condition" or event != EVENT_CONDITIONS[condition] or not granted:
                event_ok = False
                problems.append(
                    f"{sheet}/{row['Name']}: names {condition!r}, which reads a "
                    f"fact held only while {EVENT_CONDITIONS[condition]!r} is "
                    f"raised. It may be only the Condition of an action row or of "
                    f"a row scaled by own_stacks whose event is "
                    f"{EVENT_CONDITIONS[condition]!r}; anywhere else it refuses "
                    f"every time and the row grants nothing.")
        if not event_ok or (str(row.get("Condition") or "").strip() in EVENT_CONDITIONS
                            and not str(row.get("Condition2") or "").strip()):
            continue
        if not stat:
            continue
        for column in ("Condition", "Condition2"):
            condition = str(row.get(column) or "").strip()
            if not condition:
                continue
            if stat not in CONDITIONED_STATS_WITH_AN_ASKER:
                problems.append(
                    f"{sheet}/{row['Name']}: states {stat!r} under the condition "
                    f"{condition!r}, and nothing is known to ask for that stat "
                    f"through the stat pipeline. A gameplay attribute is worked "
                    f"out with every condition refused, so where the consuming "
                    f"code reads the attribute this row would grant NOTHING and "
                    f"say nothing -- which is what a resistance row under "
                    f"health_below would have done on 2026-10-05. Give the stat "
                    f"a lookup where its value is used, and add it to "
                    f"CONDITIONED_STATS_WITH_AN_ASKER here with a probe in "
                    f"CataclysmStatExemptionTests.cpp; or take the condition off.")
                continue
            needs = what_a_condition_needs(condition)
            if condition in CONDITIONS_OF_A_HIT_DEALT_AN_ASKER_DOES_NOT_PASS.get(stat, ()):
                problems.append(
                    f"{sheet}/{row['Name']}: states {stat!r} under the condition "
                    f"{condition!r}, which asks about the blow being dealt, and "
                    f"the code that asks for {stat!r} hands over the character "
                    f"struck and not the blow. The condition would be refused "
                    f"every time, so this row would grant NOTHING and say "
                    f"nothing. CONDITIONS_OF_A_HIT_DEALT_AN_ASKER_DOES_NOT_PASS "
                    f"says which.")
                continue
            if needs and needs not in CONDITIONED_STATS_WITH_AN_ASKER[stat]:
                about = ("a blow being taken" if needs == "blow_taken"
                         else "an action row's event" if needs == "action_event"
                         else "a hit being dealt")
                problems.append(
                    f"{sheet}/{row['Name']}: states {stat!r} under the condition "
                    f"{condition!r}, which asks about {about}, and the code that "
                    f"asks for {stat!r} does not hand that over. The condition "
                    f"would be refused every time, so this row would grant "
                    f"NOTHING and say nothing. CONDITIONED_STATS_WITH_AN_ASKER "
                    f"says what each stat's asker passes.")
    return problems


#: The stat a worn row grants class points on. Ruled 2026-10-09, for "Gain 5-10
#: class points" and "Gain 3-8 additional class points".
#:
#: THE GAME READS IT IN ONE PLACE AND NOT THROUGH THE STAT PIPELINE.
#: `UCataclysmPassivePoints::GrantedByWornRows` reads the worn rows' own values
#: in `UCataclysmEquipmentComponent::RefreshAttributes`, rounds each row DOWN to
#: a whole number by itself, and adds the whole numbers. It runs no pipeline, so
#: it counts a row only when the row is flat and carries no condition, no scale
#: and no required tag. `refuse_a_class_point_row_the_game_cannot_count` refuses
#: every other shape, because the game would grant nothing for it and say nothing.
CLASS_POINTS_GRANTED_STAT = "class_points_granted"

#: The fewest and the most class points one row's two figures may state.
#:
#: 1, BECAUSE THE GAME ROUNDS A ROW DOWN: a figure below 1 is a row that can
#: roll no point. 10, BECAUSE IT IS THE LARGEST FIGURE EITHER RULED SENTENCE
#: STATES ("Gain 5-10 class points"); a larger grant is a design decision, and
#: this bound is where it is made. A labelled judgement of 2026-10-09.
#:
#: THE FIGURES NEED NOT BE WHOLE. A roll between 5 and 10 is 7.3 as often as it
#: is 7, and the game rounds it down.
MIN_CLASS_POINTS_GRANTED_BY_A_ROW = 1.0
MAX_CLASS_POINTS_GRANTED_BY_A_ROW = 10.0


def refuse_a_class_point_row_the_game_cannot_count(sheet: str,
                                                   rows: list[dict]) -> list[str]:
    """A row granting class points is one the game's one reader counts.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE, for the reason
    `CLASS_POINTS_GRANTED_STAT` gives. Each refusal names what the reader
    cannot do with the row.

    A PASSIVE NODE MAY NOT GRANT THE STAT AT ALL. The points decide how much of
    the passive tree counts, so the game reads them from the worn rows before
    the tree is added, and a node's row on this stat is never read. A row built
    with a `Node` key is a passive effect row; that is how the sheet is told,
    the way `refuse_a_reach_scale_a_row_cannot_state` tells it by a key.
    """
    problems = []
    for row in rows:
        if str(row.get("Stat") or "").strip() != CLASS_POINTS_GRANTED_STAT:
            continue
        who = f"{sheet}/{row['Name']}"
        if "Node" in row:
            problems.append(
                f"{who}: a passive node grants {CLASS_POINTS_GRANTED_STAT!r}, "
                f"and the game reads class points from worn rows only, before "
                f"the passive tree is added. The row would grant NOTHING and "
                f"say nothing.")
            continue
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: grants {CLASS_POINTS_GRANTED_STAT!r} with the value "
                f"kind {kind!r}. Class points are a count, read as a flat "
                f"number and rounded down; any other kind grants NOTHING.")
        for column, what in (("RequiredTags", "a required tag"),
                             ("Condition", "a condition"),
                             ("Condition2", "a second condition"),
                             ("Scale", "a scale")):
            if str(row.get(column) or "").strip():
                problems.append(
                    f"{who}: grants {CLASS_POINTS_GRANTED_STAT!r} under "
                    f"{what}, {str(row[column]).strip()!r}. The code that "
                    f"reads class points reads the row's value and judges "
                    f"nothing else, so it counts no such row: this one would "
                    f"grant NOTHING and say nothing.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if not (MIN_CLASS_POINTS_GRANTED_BY_A_ROW <= value
                    <= MAX_CLASS_POINTS_GRANTED_BY_A_ROW):
                problems.append(
                    f"{who}: states {value:g} class points, and a row may "
                    f"state from {MIN_CLASS_POINTS_GRANTED_BY_A_ROW:g} to "
                    f"{MAX_CLASS_POINTS_GRANTED_BY_A_ROW:g}. Below 1 the game "
                    f"rounds the row down to no point; above "
                    f"{MAX_CLASS_POINTS_GRANTED_BY_A_ROW:g} is more than any "
                    f"ruled sentence grants.")
    return problems


#: The two stats a row changes a healing skill's health through, each with the
#: one value kind its reader gives a meaning to and the least and the most its
#: two figures may state. Ruled 2026-10-09.
#:
#: THE HEALING SKILLS ARE LIVING PYRE AND BLOOD PYRE, the owner's decision of
#: 2026-10-09, and they are the two skills tagged under `Stat.Recovery`.
#: `tools/tests/test_the_healing_skills_are_the_two_tagged_under_recovery.py`
#: holds that against `game/Data/WeaponSkills.csv`.
#:
#: `healing_skill_health_restored` IS A `more` ROW OF 30 TO 60, for "Healing
#: skills restore 30%-60% more HP". `UCataclysmRegeneration::HealingSkillAmount`
#: applies it to the health Living Pyre returns from a blow and to the EXTRA
#: regeneration a character gets in its own Blood Pyre.
#:
#: `healing_skill_health_as_energy_shield` IS A `flat` ROW OF 10 TO 20, for
#: "Healing skills also restore 10%-20% of the healed amount as energy shield".
#: `UCataclysmRegeneration::GiveHealingSkillShield` reads it as a percentage of
#: the health that arrived.
#:
#: THE BOUNDS ARE THE FIGURES THE TWO SENTENCES STATE. A figure outside them is
#: a design decision, and this is where it is made. A labelled judgement of
#: 2026-10-09.
#:
#: BOTH ARE ASKED WITH NO TAGS, at the two places a healing skill pays its
#: health and nowhere else. A row carrying Required Tags would match at neither
#: place, so `refuse_a_healing_skill_row_the_game_cannot_read` refuses one.
HEALING_SKILL_STATS: dict[str, tuple[str, float, float]] = {
    "healing_skill_health_restored": ("more", 30.0, 60.0),
    "healing_skill_health_as_energy_shield": ("flat", 10.0, 20.0),
}


def refuse_a_healing_skill_row_the_game_cannot_read(sheet: str,
                                                    rows: list[dict]) -> list[str]:
    """A row on one of the two healing skill stats is one its reader gives
    the meaning its sentence has.

    THE ROW IS ACCEPTED AND WRONG OR DEAD OTHERWISE, for the reasons
    `HEALING_SKILL_STATS` gives. Each refusal names what the reader would do
    with the row.

    A CONDITION OR A SCALE ON EITHER STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    neither stat is on the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE. It states a value per point and no pair
    of figures, so the bounds here do not describe it, and no ruled sentence is
    a passive node's. A row built with a `Node` key is a passive effect row.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in HEALING_SKILL_STATS or "Node" in row:
            continue
        wanted, least, most = HEALING_SKILL_STATS[stat]
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != wanted:
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"ruled sentence makes it a {wanted!r} row, and any other kind "
                f"changes a healing skill's health by a figure the sentence "
                f"does not state.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, at the two places a "
                f"healing skill pays its health, so a row requiring a tag "
                f"matches at neither: this one would grant NOTHING and say "
                f"nothing.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if not least <= value <= most:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row may state "
                    f"from {least:g} to {most:g}, the figures its ruled "
                    f"sentence states.")
    return problems


#: The stats by which a row multiplies a figure of a skill of its wearer's:
#: each with the one value kind its reader gives a meaning to and the least and
#: the most its two figures may state. Ruled 2026-10-13 under the owner's
#: delegation.
#:
#: `aura_effect` IS A `more` ROW OF 20 TO 50, for "Your auras are 20%-50% more
#: effective". `UCataclysmAuraSkill::MoreEffective` applies it to five figures
#: of an aura skill: its pulse damage, the magnitude of the named debuff it
#: lays, its ally damage bonus, the health Living Pyre returns for a hit taken
#: and its growth for a hit taken. `UCataclysmAuraSkill::AuraEffectStat` holds
#: the same name, and
#: `tools/tests/test_aura_effect_and_buff_potency_rows.py` compares them.
#:
#: THE BOUNDS ARE THE FIGURES THE SENTENCE STATES. A figure outside them is a
#: design decision, and this is where it is made. A labelled judgement of
#: 2026-10-13.
#:
#: "YOUR AURAS" IS THE AURA SHAPE, WHATEVER TAGS THE SKILL CARRIES. The game
#: asks for the stat with the aura skill's own tags, so a row requiring a tag
#: would reach only the auras that carry it: one requiring `Slot.Aura` would
#: leave Living Pyre out, which the ruling includes. So a row requiring any tag
#: is refused. A labelled judgement of 2026-10-13.
#:
#: `buff_potency` IS A `more` ROW OF 20 TO 40, for "Buff effects you apply are
#: 20%-40% more potent". `UCataclysmSelfBuffSkill::MorePotent` applies it to the
#: figure of a buff a skill of the wearer's lays: the More damage a self buff
#: grants, which is also what is copied to allies, and an aura's ally damage
#: bonus. `UCataclysmSelfBuffSkill::BuffPotencyStat` holds the same name, and
#: the same test file compares them. ITS ROW REQUIRES NO TAG EITHER: an aura's
#: ally bonus is one of its figures, and Conflagration does not carry
#: `Type.Buff`, so a row requiring that tag would leave it out.
AURA_AND_BUFF_EFFECT_STATS: dict[str, tuple[str, float, float]] = {
    "aura_effect": ("more", 20.0, 50.0),
    "buff_potency": ("more", 20.0, 40.0),
}


def refuse_an_aura_or_buff_effect_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of the stats of `AURA_AND_BUFF_EFFECT_STATS` is one its
    reader gives the meaning its sentence has.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons that list gives.
    Each refusal names what the reader would do with the row.

    A CONDITION OR A SCALE ON ANY OF THEM IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    none of them is on the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as
    `refuse_a_healing_skill_row_the_game_cannot_read` leaves one: it states a
    value per point and no pair of figures, and no ruled sentence is a passive
    node's. A row built with a `Node` key is a passive effect row.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in AURA_AND_BUFF_EFFECT_STATS or "Node" in row:
            continue
        wanted, least, most = AURA_AND_BUFF_EFFECT_STATS[stat]
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != wanted:
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"ruled sentence makes it a {wanted!r} row: the game applies "
                f"the stat to a figure of the skill's own, and any other kind "
                f"changes that figure by an amount the sentence does not "
                f"state.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with the tags of the skill whose "
                f"figure it is, so the row would reach only the skills "
                f"carrying that tag, and the ruled sentence reaches every one "
                f"of them whatever its tags.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if not least <= value <= most:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row may state "
                    f"from {least:g} to {most:g}, the figures its ruled "
                    f"sentence states.")
    return problems


#: The two stats by which a hit may make its wearer immune to that hit's damage
#: type for a time, each with the least its two figures may state, not included,
#: and the most, included. Ruled 2026-10-12 under the owner's delegation, for
#: "Shard of Anarchy (6-Piece Bonus): When you take damage, you have a 10%
#: chance to become immune to the damage type that hit you for 5 seconds".
#:
#: ONE SENTENCE IS TWO ROWS, BOTH `flat`: the chance in percent on
#: `damage_type_immunity_on_hit_chance`, 10 to 10, and the seconds on
#: `damage_type_immunity_on_hit_seconds`, 5 to 5. So the row states both
#: figures, and the game works for any chance and any time inside the bounds.
#: `UCataclysmAbilitySystemComponent::DamageTypeImmunityOnHitChanceStat` and
#: `DamageTypeImmunityOnHitSecondsStat` hold the same names, and
#: `tools/tests/test_charge_and_placed_action_names_match_the_engine.py`
#: compares them.
#:
#: THE GAME ASKS FOR BOTH IN ONE PLACE, WITH NO TAGS,
#: `UCataclysmAbilitySystemComponent::RollDamageTypeImmunityOnHit`, and opens
#: nothing unless BOTH are above nought. So a row requiring a tag matches
#: nowhere, and an enchantment stating one of the two without the other grants
#: nothing; `refuse_a_damage_type_immunity_row_the_game_cannot_read` refuses
#: both.
#:
#: THE BOUNDS. A chance above nought and up to 100: the game does not roll a
#: chance of 100 or more, so a larger figure would say nothing more. Seconds
#: above nought and up to `MAX_DAMAGE_IMMUNITY_SECONDS`, the bound of the
#: no-damage window, a labelled judgement of 2026-10-12: a longer immunity to
#: one type is a design decision, and this bound is where it is made.
DAMAGE_TYPE_IMMUNITY_ON_HIT_CHANCE_STAT = "damage_type_immunity_on_hit_chance"
DAMAGE_TYPE_IMMUNITY_ON_HIT_SECONDS_STAT = "damage_type_immunity_on_hit_seconds"
DAMAGE_TYPE_IMMUNITY_ON_HIT_STATS: dict[str, tuple[float, float]] = {
    DAMAGE_TYPE_IMMUNITY_ON_HIT_CHANCE_STAT: (0.0, 100.0),
    DAMAGE_TYPE_IMMUNITY_ON_HIT_SECONDS_STAT: (0.0, MAX_DAMAGE_IMMUNITY_SECONDS),
}


def refuse_a_damage_type_immunity_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of the two stats above is one its reader opens a window
    for.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE, for the reasons
    `DAMAGE_TYPE_IMMUNITY_ON_HIT_STATS` gives. Each refusal names what the
    reader would do with the row.

    A CONDITION OR A SCALE ON EITHER STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    neither stat is on the lists those two read.

    AND THE TWO STATS COME TOGETHER. An enchantment with a row on one and none
    on the other is refused once, by the name of the row it does have.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as
    `refuse_a_healing_skill_row_the_game_cannot_read` leaves one: it states a
    value per point and no pair of figures, and no ruled sentence is a passive
    node's. A row built with a `Node` key is a passive effect row.
    """
    problems = []
    stated: dict[str, dict[str, str]] = {}
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in DAMAGE_TYPE_IMMUNITY_ON_HIT_STATS or "Node" in row:
            continue
        above, most = DAMAGE_TYPE_IMMUNITY_ON_HIT_STATS[stat]
        who = f"{sheet}/{row['Name']}"
        stated.setdefault(str(row.get("Enchantment") or row["Name"]),
                          {}).setdefault(stat, who)
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, as a percentage chance or as "
                f"seconds, so the row must be 'flat'; any other kind works on "
                f"a base of nought and opens NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, where a hit its "
                f"wearer took is rolled, so a row requiring a tag matches "
                f"nowhere: this one would grant NOTHING and say nothing.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if not above < value <= most:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row may "
                    f"state a figure above {above:g} and up to {most:g}.")
    for held in stated.values():
        for stat in DAMAGE_TYPE_IMMUNITY_ON_HIT_STATS:
            if stat in held:
                continue
            other = next(iter(held.values()))
            problems.append(
                f"{other}: its enchantment states no row on {stat!r}. The "
                f"game opens a window only when the chance and the seconds "
                f"are both above nought, so this row alone would grant "
                f"NOTHING and say nothing.")
    return problems


#: The flag by which what a character's retaliation strikes cannot reduce or
#: mitigate it. Ruled 2026-10-12 under the owner's delegation, for
#: "Retaliation damage cannot be reduced or mitigated by the attacker".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 1 to 1, with no Required Tags, no Condition
#: and no Scale. `UCataclysmRetaliation::CannotBeMitigatedStat` holds the same
#: name, and `TestRetaliationThatCannotBeMitigated` in
#: `tools/tests/test_generate_datatables.py` compares the two.
#:
#: THE GAME ASKS FOR IT IN ONE PLACE, WITH NO TAGS,
#: `UCataclysmRetaliation::CannotBeMitigatedFor`, and reads any value above
#: nought as yes. So a row requiring a tag matches nowhere, and a row of any
#: kind but `flat` works on a base of nought and says no;
#: `refuse_a_retaliation_flag_row_the_game_cannot_read` refuses both.
RETALIATION_CANNOT_BE_MITIGATED_STAT = "retaliation_cannot_be_mitigated"


def refuse_a_retaliation_flag_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `RETALIATION_CANNOT_BE_MITIGATED_STAT` is one its reader
    answers yes to.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE, for the reasons the constant above
    gives. Each refusal names what the reader would do with the row.

    THE FIGURE IS 1 AND NOTHING ELSE. The reader takes any value above nought
    as yes, so 2 would work in the game; it is refused because a flag that
    states another number reads as a quantity, and the ruling states the row
    flat, 1 to 1.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as
    `refuse_a_damage_type_immunity_row_the_game_cannot_read` leaves one: it
    states a value per point and no pair of figures, and the ruled sentence is
    an enchantment's. A row built with a `Node` key is a passive effect row.
    """
    problems = []
    stat = RETALIATION_CANNOT_BE_MITIGATED_STAT
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself and takes any value above "
                f"nought as yes, so the row must be 'flat'; any other kind "
                f"works on a base of nought and grants NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, where retaliation is "
                f"paid, so a row requiring a tag matches nowhere: this one "
                f"would grant NOTHING and say nothing.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if value != 1.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row on this "
                    f"flag states 1 and nothing else.")
    return problems


#: How many seconds after its wearer's first hit on an enemy that enemy becomes
#: immune to the wearer's damage. Ruled 2026-10-12 under the owner's
#: delegation, for "Enemies that have been alive for more than 15 seconds
#: become immune to your damage until you disengage for 3 seconds".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 15 to 15, with no Required Tags, no
#: Condition, no Scale and no Action.
#: `UCataclysmAbilitySystemComponent::EnemiesImmuneAfterSecondsStat` holds the
#: same name, and `TestEnemiesImmuneFifteenSecondsAfterAFirstHit` in
#: `tools/tests/test_generate_datatables.py` compares the two.
#:
#: THE GAME ASKS FOR IT IN ONE PLACE, WITH NO TAGS,
#: `UCataclysmAbilitySystemComponent::IsImmuneToDamageOf`, and reads nought or
#: less as no such rule. So a row requiring a tag matches nowhere, and a row
#: of any kind but `flat` works on a base of nought and states no seconds.
#:
#: THE SENTENCE'S 3 SECONDS ARE NOT ON THE ROW. "Disengage for 3 seconds" is
#: the wearer out of combat, and how long that takes is the game's one combat
#: lapse, `UCataclysmAbilitySystemComponent::CombatLapseSeconds`, held here as
#: `COMBAT_LAPSE_SECONDS` and compared with the engine's by that test class.
#: No cell of the row is read for it.
#:
#: THE BOUND. Above nought and up to 60 seconds, the bound of the seconds
#: conditions: a labelled judgement of the writing session.
ENEMIES_IMMUNE_AFTER_SECONDS_STAT = "enemies_immune_to_you_after_seconds"
MAX_ENEMIES_IMMUNE_AFTER_SECONDS = 60.0
COMBAT_LAPSE_SECONDS = 3.0

#: The columns of a row on `ENEMIES_IMMUNE_AFTER_SECONDS_STAT` that must state
#: nothing, each a place somebody might write the sentence's 3 seconds.
COLUMNS_THE_IMMUNITY_ROW_DOES_NOT_READ = (
    "ConditionValue", "ConditionValue2", "ConditionValueHigh", "StackSeconds",
    "StackSecondsHigh", "EverySeconds", "TriggerCooldown", "EventValue")


def refuse_an_enemies_immune_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `ENEMIES_IMMUNE_AFTER_SECONDS_STAT` is one figure its reader
    takes as the seconds, and it states nothing about the 3 seconds.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constant
    above gives. Each refusal names what the reader would do with the row.

    ONE FIGURE. The sentence states 15 and no range, so the two value columns
    agree. An enchantment states the stat on ONE row: two flat rows are added
    together by the stat pipeline, so a second row holding the 3 would make
    the wait 18 seconds and say nothing.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as
    `refuse_a_retaliation_flag_row_the_game_cannot_read` leaves one.
    """
    problems = []
    stat = ENEMIES_IMMUNE_AFTER_SECONDS_STAT
    lapse = (f"The sentence's {COMBAT_LAPSE_SECONDS:g} seconds of disengaging "
             f"are the game's own combat lapse (CombatLapseSeconds) and are "
             f"not a cell of this row")
    rows_of: dict[str, int] = {}
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        owner = str(row.get("Enchantment") or row["Name"])
        rows_of[owner] = rows_of.get(owner, 0) + 1
        if rows_of[owner] == 2:
            problems.append(
                f"{sheet}/{owner}: states {stat!r} on more than one row. Flat "
                f"rows on one stat are added together, so the enemy would "
                f"become immune after the SUM of them. {lapse}: state the "
                f"seconds once.")
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself as the seconds, so the row "
                f"must be 'flat'; any other kind works on a base of nought "
                f"and makes NOTHING immune.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, where a blow on the "
                f"enemy is resolved, so a row requiring a tag matches "
                f"nowhere: this one would make NOTHING immune and say "
                f"nothing.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if low != high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the row "
                f"states ONE figure, the seconds after the first hit, in both "
                f"value columns. {lapse}.")
        for value in sorted({low, high}):
            if not 0.0 < value <= MAX_ENEMIES_IMMUNE_AFTER_SECONDS:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the seconds "
                    f"must be above 0 and at most "
                    f"{MAX_ENEMIES_IMMUNE_AFTER_SECONDS:g}. Nought or less is "
                    f"read as no such rule.")
        for column in COLUMNS_THE_IMMUNITY_ROW_DOES_NOT_READ:
            written = float(row.get(column) or 0.0)
            if written != 0.0:
                problems.append(
                    f"{who}: states {written:g} in {column} beside {stat!r}, "
                    f"and nothing reads that cell for this stat. {lapse}.")
        if str(row.get("Action") or "").strip():
            problems.append(
                f"{who}: states {stat!r} on a row with the action "
                f"{str(row.get('Action')).strip()!r}. The stat is read as a "
                f"stat line and an action row states none, so this row would "
                f"make NOTHING immune.")
    return problems


#: The share of the damage that would reach its wearer's health that is taken
#: later instead, as a percent, and the seconds that share is drained over.
#: Ruled 2026-10-12 under the owner's delegation, for "25%-50% of damage taken
#: is dealt over 4-8 seconds".
#:
#: ONE SENTENCE IS TWO ROWS ON ONE ENCHANTMENT, one on each stat: `flat`, the
#: share 25 to 50 and the seconds 4 to 8, with no Required Tags, no Condition,
#: no Scale and no Action. `UCataclysmDelayedDamage::SharePercentStat` and
#: `SecondsStat` hold the same names, and
#: `TestAShareOfDamageTakenIsDealtOverSeconds` in
#: `tools/tests/test_generate_datatables.py` compares them.
#:
#: BOTH FIGURES COME FROM THE ITEM'S ONE ROLL, which is why two rows on one
#: enchantment are the shape. `AccumulateEnchantmentsInto` of
#: `UCataclysmItemModifiers` rolls every row of an enchantment with the one
#: roll its item recorded for that half. So a low roll is 25% over 4 seconds
#: and a high roll 50% over 8, ruled. Two enchantments would be two rolls.
#:
#: THE GAME ASKS FOR BOTH IN ONE PLACE, WITH NO TAGS,
#: `UCataclysmDelayedDamage::DelayShare`, where a blow's damage reaches health,
#: and delays nothing when either is nought or less. So a row requiring a tag
#: matches nowhere, a row of any kind but `flat` works on a base of nought,
#: and an enchantment stating one of the two without the other does nothing.
#:
#: THE BOUNDS. The share above nought and at most 100, which the game holds it
#: to. The seconds above nought and at most 60, the bound of the seconds
#: conditions: a labelled judgement of the writing session.
DAMAGE_TAKEN_DELAYED_PERCENT_STAT = "damage_taken_delayed_percent"
DAMAGE_TAKEN_DELAYED_SECONDS_STAT = "damage_taken_delayed_seconds"
MAX_DAMAGE_TAKEN_DELAYED_PERCENT = 100.0
MAX_DAMAGE_TAKEN_DELAYED_SECONDS = 60.0


def refuse_a_delayed_damage_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `DAMAGE_TAKEN_DELAYED_PERCENT_STAT` or on
    `DAMAGE_TAKEN_DELAYED_SECONDS_STAT` is a figure its one reader takes as
    it stands, and an enchantment states the two together, once each.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constants
    above give. Each refusal names what the reader would do with the row.

    A RANGE IS ALLOWED AND MUST RISE: the low column is not above the high
    one. The game rolls from the first column to the second, and both figures
    rise together on the one roll, ruled; a row written 8 to 4 would give the
    longest spread to the lowest roll.

    BOTH OR NEITHER STATE A RANGE. One row ranged and the other a single
    figure is refused: the sentence states two ranges, and the two rows of
    one sentence either both roll or both do not.

    A CONDITION OR A SCALE ON EITHER STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    neither stat is on the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as
    `refuse_an_enemies_immune_row_the_game_cannot_read` leaves one.
    """
    problems = []
    share = DAMAGE_TAKEN_DELAYED_PERCENT_STAT
    seconds = DAMAGE_TAKEN_DELAYED_SECONDS_STAT
    tops = {share: MAX_DAMAGE_TAKEN_DELAYED_PERCENT,
            seconds: MAX_DAMAGE_TAKEN_DELAYED_SECONDS}
    rows_of: dict[str, dict[str, list[dict]]] = {}
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in tops or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        owner = str(row.get("Enchantment") or row["Name"])
        rows_of.setdefault(owner, {share: [], seconds: []})[stat].append(row)
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; "
                f"any other kind works on a base of nought and delays "
                f"NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, where a blow's damage "
                f"reaches health, so a row requiring a tag matches nowhere: "
                f"this one would delay NOTHING and say nothing.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise. The share and the seconds rise together "
                f"on the item's one roll, so the lowest roll is the first "
                f"column of both rows.")
        for value in sorted({low, high}):
            if not 0.0 < value <= tops[stat]:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the figure "
                    f"must be above 0 and at most {tops[stat]:g}. Nought or "
                    f"less delays nothing.")
        if str(row.get("Action") or "").strip():
            problems.append(
                f"{who}: states {stat!r} on a row with the action "
                f"{str(row.get('Action')).strip()!r}. The stat is read as a "
                f"stat line and an action row states none, so this row would "
                f"delay NOTHING.")
    for owner, held in rows_of.items():
        for stat, other in ((share, seconds), (seconds, share)):
            if len(held[stat]) > 1:
                problems.append(
                    f"{sheet}/{owner}: states {stat!r} on more than one row. "
                    f"Flat rows on one stat are added together, so the game "
                    f"would read the SUM of them: state it once.")
            if held[stat] and not held[other]:
                problems.append(
                    f"{sheet}/{owner}: states {stat!r} and not {other!r}. "
                    f"The game delays nothing unless the wearer holds both, "
                    f"and both must be rows of ONE enchantment so that the "
                    f"item's one roll sets the two together.")
        if len(held[share]) == 1 and len(held[seconds]) == 1:
            ranged = [float(row.get("ValueLow") or 0.0)
                      != float(row.get("ValueHigh") or 0.0)
                      for row in (held[share][0], held[seconds][0])]
            if ranged[0] != ranged[1]:
                problems.append(
                    f"{sheet}/{owner}: states a range on one of {share!r} "
                    f"and {seconds!r} and a single figure on the other. The "
                    f"two rise together on the item's one roll, so both rows "
                    f"state a range or neither does.")
    return problems


#: The stat of "Applying a debuff to an enemy reduces their damage output by
#: 10%-20%", ruled 2026-10-13: the percent less a character deals while it
#: carries a debuff the wearer applied.
#:
#: THE GAME ASKS FOR IT IN ONE PLACE, WITH NO TAGS, `UCataclysmDebuffs::NoteLaid`,
#: where a debuff is put on another character, and records nothing when it is
#: nought or less. So a row requiring a tag matches nowhere, and a row of any
#: kind but `flat` works on a base of nought. The figure is a percent taken off
#: what the carrier's attacks are worth, which the game holds to 100.
DEBUFFED_ENEMIES_DEAL_LESS_PERCENT_STAT = "debuffed_enemies_deal_less_percent"
MAX_DEBUFFED_ENEMIES_DEAL_LESS_PERCENT = 100.0


def refuse_a_debuff_cut_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `DEBUFFED_ENEMIES_DEAL_LESS_PERCENT_STAT` is one figure its one
    reader takes as it stands, and an enchantment states it once.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constant
    above gives. Each refusal names what the reader would do with the row.

    A RANGE IS ALLOWED AND MUST RISE, as the sentence's does: more is better
    for the wearer, and the game rolls from the first column to the second.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as
    `refuse_a_delayed_damage_row_the_game_cannot_read` leaves one.
    """
    problems = []
    stat = DEBUFFED_ENEMIES_DEAL_LESS_PERCENT_STAT
    top = MAX_DEBUFFED_ENEMIES_DEAL_LESS_PERCENT
    stated: dict[str, int] = {}
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        owner = str(row.get("Enchantment") or row["Name"])
        stated[owner] = stated.get(owner, 0) + 1
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; "
                f"any other kind works on a base of nought and cuts NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, where a debuff is put "
                f"on another character, so a row requiring a tag matches "
                f"nowhere: this one would cut NOTHING and say nothing.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the lowest roll is the first column, and "
                f"a larger cut is the better roll.")
        for value in sorted({low, high}):
            if not 0.0 < value <= top:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the figure "
                    f"must be above 0 and at most {top:g}. Nought or less "
                    f"cuts nothing.")
        if str(row.get("Action") or "").strip():
            problems.append(
                f"{who}: states {stat!r} on a row with the action "
                f"{str(row.get('Action')).strip()!r}. The stat is read as a "
                f"stat line and an action row states none, so this row would "
                f"cut NOTHING.")
    for owner, count in stated.items():
        if count > 1:
            problems.append(
                f"{sheet}/{owner}: states {stat!r} on more than one row. "
                f"Flat rows on one stat are added together, so the game "
                f"would read the SUM of them: state it once.")
    return problems


#: The flag of "You are immune to debuffs but also cannot apply them", ruled
#: 2026-10-13, reading A: its holder is given no debuff and lays none.
#:
#: THE GAME ASKS WHETHER IT IS ABOVE NOUGHT, WITH NO TAGS,
#: `UCataclysmDebuffs::HoldsImmuneAndAppliesNone`, where a debuff is about to be
#: put on a character. So a row requiring a tag matches nowhere, and a row of
#: any kind but `flat` works on a base of nought.
IMMUNE_TO_DEBUFFS_AND_APPLIES_NONE_STAT = "immune_to_debuffs_and_applies_none"


def refuse_a_debuff_immunity_flag_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `IMMUNE_TO_DEBUFFS_AND_APPLIES_NONE_STAT` is a flag: flat, 1
    to 1, with no required tag and no action.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constant
    above gives. Each refusal names what the reader would do with the row.

    A FIGURE OTHER THAN 1 IS REFUSED though the game reads any value above
    nought as yes, as `refuse_a_retaliation_flag_row_the_game_cannot_read`
    refuses one: a flag that states another number reads as a quantity.

    A CONDITION OR A SCALE ON THE FLAG IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the flag is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as
    `refuse_a_debuff_cut_row_the_game_cannot_read` leaves one.
    """
    problems = []
    stat = IMMUNE_TO_DEBUFFS_AND_APPLIES_NONE_STAT
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game asks whether the flag is above nought, so the row "
                f"must be 'flat'; any other kind works on a base of nought "
                f"and refuses NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this flag with no tags, where a debuff is "
                f"about to be laid, so a row requiring a tag matches "
                f"nowhere: this one would refuse NOTHING and say nothing.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if (low, high) != (1.0, 1.0):
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and a flag "
                f"is 1 to 1. The game reads any figure above nought as yes, "
                f"so another figure reads as a quantity and is none.")
        if str(row.get("Action") or "").strip():
            problems.append(
                f"{who}: states {stat!r} on a row with the action "
                f"{str(row.get('Action')).strip()!r}. The flag is read as a "
                f"stat line and an action row states none, so this row would "
                f"refuse NOTHING.")
    return problems


#: The chance, in percent, that a damage over time ailment its wearer applied
#: explodes when it ends by time. Ruled 2026-10-13 under the owner's
#: delegation, for "Your burn effects have a 20%-40% chance to explode for
#: double their remaining damage when they expire".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 20 to 40, Required Tags
#: `Keyword.DoT.Burn`, with no Condition, no Scale and no Action.
#: `UCataclysmSkillEffects::DotExplodesAtExpiryChanceStat` holds the same
#: name, and `TestABurnThatEndsByTimeMayExplode` in
#: `tools/tests/test_generate_datatables.py` compares the two.
#:
#: THE REQUIRED TAG IS HOW THE ROW NAMES ITS AILMENT. The game asks the
#: applier for this stat in one place, in `CataclysmSkillEffects.cpp`, when a
#: damage over time effect ends by time, with the tags that effect granted:
#: its ailment's tag and nothing else. So a row requiring `Keyword.DoT.Burn`
#: reaches a Burn alone. A row requiring no tag would reach EVERY ailment
#: laid as a fixed amount a tick, and a row requiring any other tag would
#: reach another ailment or none: only Burn is ruled, and
#: `EXPLODING_AILMENT_TAGS` holds what is.
#:
#: THE BOUNDS. Above nought and at most 100: the game compares a chance of
#: 100 or more and does not roll it. The range rises, because the higher
#: roll of an item is the better one and a higher chance is better for the
#: wearer.
DOT_EXPLODES_AT_EXPIRY_CHANCE_STAT = "dot_explodes_at_expiry_chance"
EXPLODING_AILMENT_TAGS = ("Keyword.DoT.Burn",)
MAX_DOT_EXPLODES_AT_EXPIRY_CHANCE = 100.0


def refuse_an_exploding_ailment_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `DOT_EXPLODES_AT_EXPIRY_CHANCE_STAT` is a chance its one
    reader takes as it stands, for the one ailment its required tag names.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constants
    above give. Each refusal names what the reader would do with the row.

    EXACTLY ONE REQUIRED TAG, AND IT IS ONE OF `EXPLODING_AILMENT_TAGS`. No
    tag is refused because the row would then make every ailment explode,
    and two tags because the ended effect grants one ailment's tag and the
    row would match nothing.

    A RANGE IS ALLOWED AND MUST RISE. An enchantment states the stat on ONE
    row: two flat rows are added together by the stat pipeline, so a second
    row of 20 to 40 would make the chance 40 to 80 and say nothing.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as
    `refuse_an_enemies_immune_row_the_game_cannot_read` leaves one.
    """
    problems = []
    stat = DOT_EXPLODES_AT_EXPIRY_CHANCE_STAT
    allowed = ", ".join(EXPLODING_AILMENT_TAGS)
    rows_of: dict[str, int] = {}
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        owner = str(row.get("Enchantment") or row["Name"])
        rows_of[owner] = rows_of.get(owner, 0) + 1
        if rows_of[owner] == 2:
            problems.append(
                f"{sheet}/{owner}: states {stat!r} on more than one row. Flat "
                f"rows on one stat are added together, so the ailment would "
                f"explode at the SUM of their chances: state it once.")
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself as the chance, so the row "
                f"must be 'flat'; any other kind works on a base of nought "
                f"and explodes NOTHING.")
        tags = [tag.strip()
                for tag in str(row.get("RequiredTags") or "").split(",")
                if tag.strip()]
        if not tags:
            problems.append(
                f"{who}: states {stat!r} under no required tag. The game "
                f"asks for this stat with the tag of the ailment that ended, "
                f"so a row requiring none would make EVERY damage over time "
                f"ailment explode. Only these are ruled: {allowed}.")
        elif len(tags) > 1:
            problems.append(
                f"{who}: states {stat!r} under {len(tags)} required tags, "
                f"{', '.join(tags)!r}. The game asks with the one tag the "
                f"ended ailment granted, so a row requiring two matches "
                f"nowhere: this one would explode NOTHING and say nothing.")
        elif tags[0] not in EXPLODING_AILMENT_TAGS:
            problems.append(
                f"{who}: states {stat!r} under the required tag "
                f"{tags[0]!r}. The row names its ailment by that tag, and "
                f"only these are ruled to explode: {allowed}.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise. The game rolls from the first column to "
                f"the second and the higher roll is the better one, so the "
                f"lowest chance is the first column.")
        for value in sorted({low, high}):
            if not 0.0 < value <= MAX_DOT_EXPLODES_AT_EXPIRY_CHANCE:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the chance "
                    f"must be above 0 and at most "
                    f"{MAX_DOT_EXPLODES_AT_EXPIRY_CHANCE:g}. Nought or less "
                    f"is no chance.")
        if str(row.get("Action") or "").strip():
            problems.append(
                f"{who}: states {stat!r} on a row with the action "
                f"{str(row.get('Action')).strip()!r}. The stat is read as a "
                f"stat line and an action row states none, so this row would "
                f"explode NOTHING.")
    return problems


#: The tag a row about "your gadgets" is scoped by, as the merged gadget rows
#: are: "Gadgets have 40%-70% increased HP", "Gadgets fire 20%-40% faster".
#:
#: `Type.Gadget` SINCE THE OWNER'S RULING OF 2026-11-25 ("agree with your
#: rec"), which moved the 18 enchantments that say "gadget" off
#: `Type.Deployable`. Every machine that is not a trap carries `Type.Gadget`
#: (the Saboteur's rows, 2026-11-25), a Bolt Turret and a Ballista included, so
#: the tag itself now leaves a Spike Trap out. The game still asks
#: `ACataclysmMinion::IsGadget` first in `ACataclysmMinion::SummonersGadgetStat`.
#:
#: UNTIL THEN `Type.Deployable`, which a Spike Trap carries too: the code alone
#: left the trap out.
GADGET_ROW_TAG = "Type.Gadget"

#: The flag by which a summoner's gadgets choose the enemy with the most
#: current health within their reach. Ruled 2026-10-13 by the coordinating
#: session under the owner's delegation, for "Your gadgets prioritize the
#: highest HP enemy within their range instead of the nearest".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 1 to 1, Required Tags `Type.Deployable`,
#: with no Condition, no Scale and no Action.
#: `ACataclysmMinion::GadgetsTargetMostHealthStat` holds the same name, and
#: `TestGadgetsChooseTheMostHealth` in
#: `tools/tests/test_generate_datatables.py` compares the two.
GADGETS_TARGET_MOST_HEALTH_STAT = "gadgets_target_most_health"

#: The flag by which a summoner's gadget that kills an enemy with a blow of
#: its own strikes once more, at the nearest enemy within its reach. Ruled
#: 2026-10-13 by the coordinating session under the owner's delegation, for
#: "When a gadget kills an enemy, it fires an additional burst at the nearest
#: enemy within its range".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 1 to 1, Required Tags `Type.Deployable`,
#: with no Condition, no Scale, NO ACTION AND NO ACTION EVENT. The sentence's
#: own tags are `Type.Deployable, Trigger.OnKill`; the row requires the first
#: alone, because the game asks with the gadget's type tags and no gadget
#: carries `Trigger.OnKill`. No row event is raised for a gadget's kill: the
#: burst is the gadget's own act, in `ACataclysmMinion::AttackTarget`.
#: `ACataclysmMinion::GadgetsBurstOnKillStat` holds the same name, and
#: `TestGadgetsBurstOnAKill` in `tools/tests/test_generate_datatables.py`
#: compares the two.
GADGETS_BURST_ON_KILL_STAT = "gadgets_burst_on_kill"

#: The flag by which a summoner's gadgets take no damage from any blow, and
#: the percent of a gadget's maximum health as it was placed that it loses
#: each second. Ruled 2026-10-13 by the coordinating session under the
#: owner's delegation, for "Your gadgets are invincible but lose 5% of their
#: maximum HP per second passively".
#:
#: ONE SENTENCE IS TWO ROWS ON ONE ENCHANTMENT, both `flat` with Required Tags
#: `Type.Deployable`, no Condition, no Scale and no Action: the flag 1 to 1,
#: and the percent 5 to 5. `ACataclysmMinion::GadgetsTakeNoDamageStat` and
#: `GadgetHealthLostPercentPerSecondStat` hold the same names, and
#: `TestGadgetsTakeNoDamageAndAreDrained` in
#: `tools/tests/test_generate_datatables.py` compares them.
#:
#: THE GAME READS EACH BY ITSELF, in `ACataclysmMinion::StepNoDamageAndDrain`,
#: so either would work alone. AN ENCHANTMENT THAT STATES ONE WITHOUT THE OTHER
#: IS REFUSED ALL THE SAME: gadgets that take no damage and lose nothing is a
#: design nobody ruled, and a drain with nothing for it is a drawback written
#: among the benefits. A labelled judgement of the writing session.
#:
#: THE PERCENT IS ONE FIGURE, above nought and at most 100. The sentence
#: states 5 and no range, and which way a range of a cost inside a benefit
#: should roll has not been ruled.
GADGETS_TAKE_NO_DAMAGE_STAT = "gadgets_take_no_damage"
GADGET_HEALTH_LOST_PERCENT_PER_SECOND_STAT = (
    "gadget_health_lost_percent_per_second")
MAX_GADGET_HEALTH_LOST_PERCENT_PER_SECOND = 100.0

#: The flag by which a summoner's gadgets cast a copy of the last skill the
#: summoner used, at their own target, every 2 seconds. Ruled 2026-10-14 by
#: the coordinating session on the enchantment session's plan, with the
#: owner's option B ("the WHOLE skill is cast from the gadget"), for "Gadgets
#: copy the last skill you used and fire it at their target once every 2
#: seconds".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 1 to 1, Required Tags `Type.Deployable`,
#: with no Condition, no Scale, no Action and no Action Event, as the other
#: gadget flags. THE 2 SECONDS ARE THE ENGINE'S,
#: `ACataclysmMinion::SecondsBetweenSkillCopies`, and not the row's: the row is
#: a flag. `ACataclysmMinion::GadgetsCopyYourLastSkillStat` holds the same
#: name, and `TestGadgetsCopyTheLastSkillUsed` in
#: `tools/tests/test_generate_datatables.py` compares the two.
GADGETS_COPY_YOUR_LAST_SKILL_STAT = "gadgets_copy_your_last_skill"

#: The stats a summoner's row gives its gadgets that are flags: a flat 1, read
#: as above nought.
GADGET_FLAG_STATS = (
    GADGETS_TARGET_MOST_HEALTH_STAT,
    GADGETS_BURST_ON_KILL_STAT,
    GADGETS_TAKE_NO_DAMAGE_STAT,
    GADGETS_COPY_YOUR_LAST_SKILL_STAT,
)

#: How many seconds apart a summoner's gadgets fire at every enemy within
#: their reach. Ruled 2026-10-13 by the coordinating session under the
#: owner's delegation, for "Gadgets fire at all enemies within range
#: simultaneously once every 5-8 seconds".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, Required Tags `Type.Deployable`, with no
#: Condition, no Scale and no Action. THE SHEET STATES IT AS THE SENTENCE DOES,
#: Value Low 5 and Value High 8, AND THE SENTENCE IS MARKED `1` IN ITS
#: `Rolls Down` CELL, so the pair leaves `enchantment_effects` as 8 to 5 and a
#: roll of 1 is the shorter wait: a better roll gives the better outcome, the
#: owner's rule of 2026-10-05. `ACataclysmMinion::GadgetVolleyEverySecondsStat`
#: holds the same name, and `TestGadgetsFireAVolley` in
#: `tools/tests/test_generate_datatables.py` compares the two.
#:
#: THE GAME READS THE ROWS THEMSELVES AND TAKES THE SHORTEST,
#: `ACataclysmMinion::VolleyEverySeconds`, and not the sum the stat pipeline
#: would make of them. So a second row on one enchantment would do nothing.
#:
#: THE BOUND. Above nought and up to 60 seconds, the bound of the seconds
#: conditions: a labelled judgement of the writing session.
GADGET_VOLLEY_EVERY_SECONDS_STAT = "gadget_volley_every_seconds"
MAX_GADGET_VOLLEY_EVERY_SECONDS = 60.0

#: Every stat a summoner's row gives its gadgets. Each is read off the
#: summoner with the gadget's own type tags and no base, by
#: `ACataclysmMinion::SummonersGadgetStat` or, for the volley's seconds, by
#: `ACataclysmMinion::VolleyEverySeconds`. The loss each second is not a flag
#: and is read by the first of those.
GADGET_STATS = GADGET_FLAG_STATS + (
    GADGET_VOLLEY_EVERY_SECONDS_STAT,
    GADGET_HEALTH_LOST_PERCENT_PER_SECOND_STAT,
)


def refuse_a_gadget_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of `GADGET_STATS` is one its reader takes as it stands.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE. The game reads each of these
    stats as the figure itself, off the summoner, with the gadget's type
    tags: `ACataclysmMinion::SummonersGadgetStat`, and for the volley's
    seconds `ACataclysmMinion::VolleyEverySeconds`, which passes over a row
    that is not flat. So a row of any kind but `flat` gives nothing, and a
    row on an action states no stat line. Each refusal names what the reader
    would do with the row.

    THE REQUIRED TAGS ARE `GADGET_ROW_TAG` AND NOTHING ELSE. A row with no tag
    would work in the game, because the reader answers nought for everything
    that is not a gadget whatever the row requires. It is refused so that
    every row about gadgets states its scope the way the merged ones do: a
    labelled judgement of the writing session. A row requiring a tag a Bolt
    Turret and a Ballista do not both carry would reach one of them or
    neither, and is refused for that.

    A FLAG STATES 1 AND NOTHING ELSE, as
    `refuse_a_retaliation_flag_row_the_game_cannot_read` holds its flag, and
    for its reason.

    THE VOLLEY'S SECONDS FALL AS THE ROLL RISES, and are stated once. The
    game rolls from the first column to the second, and a roll of 1 must be
    the shorter wait, so the first column is not below the second: a pair
    that rises is a sentence whose range was not marked in `Rolls Down`. An
    enchantment states the seconds on ONE row, because the game takes the
    shortest of the rows and a second one would do nothing.

    NO DAMAGE AND THE LOSS EACH SECOND ARE STATED TOGETHER, ONCE EACH, ON ONE
    ENCHANTMENT, and the loss is one figure above nought and at most 100, for
    the reasons `GADGETS_TAKE_NO_DAMAGE_STAT` gives.

    A CONDITION OR A SCALE ON ANY OF THESE STATS IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    none is on the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as that function leaves one.
    """
    problems = []
    volley_rows_of: dict[str, int] = {}
    no_damage = GADGETS_TAKE_NO_DAMAGE_STAT
    drain = GADGET_HEALTH_LOST_PERCENT_PER_SECOND_STAT
    pair = (no_damage, drain)
    pair_rows_of: dict[str, dict[str, int]] = {}
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in GADGET_STATS or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        if stat == GADGET_VOLLEY_EVERY_SECONDS_STAT:
            owner = str(row.get("Enchantment") or row["Name"])
            volley_rows_of[owner] = volley_rows_of.get(owner, 0) + 1
            if volley_rows_of[owner] == 2:
                problems.append(
                    f"{sheet}/{owner}: states {stat!r} on more than one row. "
                    f"The game takes the SHORTEST of the rows, so every row "
                    f"but that one would do nothing: state the seconds once.")
            low = float(row.get("ValueLow") or 0.0)
            high = float(row.get("ValueHigh") or 0.0)
            if low < high:
                problems.append(
                    f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                    f"pair must fall. A roll of 1 gives the second figure, "
                    f"and the best roll is the SHORTEST wait: mark the "
                    f"sentence's range in the Rolls Down column of the "
                    f"Enchantments sheet, which writes the pair the other "
                    f"way round.")
            for value in sorted({low, high}):
                if not 0.0 < value <= MAX_GADGET_VOLLEY_EVERY_SECONDS:
                    problems.append(
                        f"{who}: states {value:g} on {stat!r}, and the "
                        f"seconds must be above 0 and at most "
                        f"{MAX_GADGET_VOLLEY_EVERY_SECONDS:g}. Nought or less "
                        f"is read as no such rule.")
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself off the summoner, so the row "
                f"must be 'flat'; any other kind works on a base of nought "
                f"and gives a gadget NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags != GADGET_ROW_TAG:
            problems.append(
                f"{who}: states {stat!r} under the required tags {tags!r}. A "
                f"row about gadgets requires {GADGET_ROW_TAG!r} and nothing "
                f"else, as the merged gadget rows do; the game leaves a Spike "
                f"Trap out by itself.")
        if str(row.get("Action") or "").strip():
            problems.append(
                f"{who}: states {stat!r} on a row with the action "
                f"{str(row.get('Action')).strip()!r}. The stat is read as a "
                f"stat line and an action row states none, so this row would "
                f"give a gadget NOTHING.")
        if stat in GADGET_FLAG_STATS:
            for column in ("ValueLow", "ValueHigh"):
                value = float(row.get(column) or 0.0)
                if value != 1.0:
                    problems.append(
                        f"{who}: states {value:g} on {stat!r}, and a row on "
                        f"this flag states 1 and nothing else.")
        if stat in pair:
            owner = str(row.get("Enchantment") or row["Name"])
            pair_rows_of.setdefault(
                owner, {name: 0 for name in pair})[stat] += 1
        if stat == drain:
            low = float(row.get("ValueLow") or 0.0)
            high = float(row.get("ValueHigh") or 0.0)
            if low != high:
                problems.append(
                    f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                    f"row states ONE figure in both value columns. Which way "
                    f"a range of this loss should roll has not been ruled.")
            for value in sorted({low, high}):
                if not 0.0 < value <= MAX_GADGET_HEALTH_LOST_PERCENT_PER_SECOND:
                    problems.append(
                        f"{who}: states {value:g} on {stat!r}, and the "
                        f"percent must be above 0 and at most "
                        f"{MAX_GADGET_HEALTH_LOST_PERCENT_PER_SECOND:g}. "
                        f"Nought or less drains nothing.")
    for owner, held in pair_rows_of.items():
        for stat, other in ((no_damage, drain), (drain, no_damage)):
            if held[stat] > 1:
                problems.append(
                    f"{sheet}/{owner}: states {stat!r} on more than one row. "
                    f"Flat rows on one stat are added together, so the game "
                    f"would read the SUM of them: state it once.")
            if held[stat] and not held[other]:
                problems.append(
                    f"{sheet}/{owner}: states {stat!r} and not {other!r}. "
                    f"The sentence gives its gadgets both, no damage and a "
                    f"loss each second, as two rows of ONE enchantment; one "
                    f"without the other is a design nobody ruled.")
    return problems


#: The flag by which a skill in the Ultimate slot has no cooldown and a mana
#: cost that doubles with each paid use of it in the dungeon being walked.
#: Ruled 2026-10-14 by the coordinating session, for "Mana Weaver (10-Piece
#: Bonus): Your ultimate ability no longer has a cooldown. Instead, its mana
#: cost is doubled every time you use it. This cost resets at the beginning of
#: every dungeon".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 1 to 1, with no Required Tags, no Condition,
#: no Scale and no Action. The doubling, its count, its cap of 30 and its reset
#: are the engine's and not the row's.
#: `UCataclysmGameplayAbility::UltimateCostDoublesInsteadOfCooldownStat` holds
#: the same name, and `TestUltimateCostDoublesInsteadOfCooldown` in
#: `tools/tests/test_generate_datatables.py` compares the two.
#:
#: THE GAME ASKS FOR IT IN ONE PLACE, WITH NO TAGS,
#: `UCataclysmGameplayAbility::UltimateCostDoublesByARow`, because the slot the
#: skill holds decides what "your ultimate ability" is and not a tag of the
#: skill's row; it reads any value above nought as yes. So a row requiring a
#: tag matches nowhere, a row of any kind but `flat` works on a base of nought
#: and says no, and a row with an action is not a stat line at all;
#: `refuse_an_ultimate_cost_doubling_row_the_game_cannot_read` refuses each.
ULTIMATE_COST_DOUBLES_INSTEAD_OF_COOLDOWN_STAT = (
    "ultimate_cost_doubles_instead_of_cooldown")


def refuse_an_ultimate_cost_doubling_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `ULTIMATE_COST_DOUBLES_INSTEAD_OF_COOLDOWN_STAT` is one its
    reader answers yes to.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE, for the reasons the constant above
    gives. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_retaliation_flag_row_the_game_cannot_read`, with an action
    refused as well.

    THE FIGURE IS 1 AND NOTHING ELSE. The reader takes any value above nought
    as yes, so 2 would work in the game; it is refused because a flag that
    states another number reads as a quantity, and the ruling states the row
    flat, 1 to 1. Least of all a number of doublings, which the engine counts.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as the retaliation flag's check leaves
    one: the ruled sentence is an enchantment's.
    """
    problems = []
    stat = ULTIMATE_COST_DOUBLES_INSTEAD_OF_COOLDOWN_STAT
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself and takes any value above "
                f"nought as yes, so the row must be 'flat'; any other kind "
                f"works on a base of nought and grants NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, because the slot a "
                f"skill holds decides what the Ultimate is, so a row requiring "
                f"a tag matches nowhere: this one would grant NOTHING and say "
                f"nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The flag "
                f"is a stat line the game asks for, and an action row is not "
                f"one.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if value != 1.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row on this "
                    f"flag states 1 and nothing else.")
    return problems


#: The percent of what reaches a nearby ally's health that its wearer takes, as
#: a blow of its own. Ruled 2026-10-15 by the coordinating session, for "You
#: take 5%-15% damage done to nearby allies".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 5 to 15, with no Required Tags, no
#: Condition, no Scale and no Action (R8). Who is an ally, the 5 m, the
#: delivery and what the share raises are the engine's and not the row's.
#: `UCataclysmAllyDamageShare::PercentStat` holds the same name, and
#: `TestAShareOfDamageDoneToNearbyAllies` in
#: `tools/tests/test_generate_datatables.py` compares the two.
#:
#: THE GAME ASKS FOR IT IN ONE PLACE, WITH NO TAGS,
#: `UCataclysmAllyDamageShare::PercentHeldBy`, from the damage branch of the
#: vital attribute set, where no skill of the wearer's is in hand. So a row
#: requiring a tag matches nowhere, a row of any kind but `flat` works on a base
#: of nought, and a row with an action is not a stat line at all.
#:
#: THE BOUND IS THE ENGINE'S: above nought and at most 100, which
#: `PercentHeldBy` holds the figure to.
DAMAGE_DONE_TO_NEARBY_ALLIES_TAKEN_PERCENT_STAT = (
    "damage_done_to_nearby_allies_taken_percent")
MAX_DAMAGE_DONE_TO_NEARBY_ALLIES_TAKEN_PERCENT = 100.0


def refuse_a_share_of_allies_damage_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `DAMAGE_DONE_TO_NEARBY_ALLIES_TAKEN_PERCENT_STAT` is a figure
    its one reader takes as it stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constant above
    gives. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_delayed_damage_row_the_game_cannot_read`, for one stat.

    A RANGE IS ALLOWED AND MUST RISE: the game rolls from the first column to
    the second.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as the delayed damage check leaves one:
    the ruled sentence is an enchantment's.
    """
    problems = []
    stat = DAMAGE_DONE_TO_NEARBY_ALLIES_TAKEN_PERCENT_STAT
    top = MAX_DAMAGE_DONE_TO_NEARBY_ALLIES_TAKEN_PERCENT
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; "
                f"any other kind works on a base of nought and shares "
                f"NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, where a blow on an "
                f"ally reaches its health, so a row requiring a tag matches "
                f"nowhere: this one would share NOTHING and say nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the game rolls from the first column to "
                f"the second.")
        for value in sorted({low, high}):
            if not 0.0 < value <= top:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the figure "
                    f"must be above 0 and at most {top:g}. Nought or less "
                    f"shares nothing, and the game holds the figure to "
                    f"{top:g}.")
    return problems


#: The percent of a point-blank skill's own blow, priced for an ally, that each
#: ally inside the skill's area is struck for. Ruled 2026-10-15 by the
#: coordinating session, for "Your AOE skills deal 50%-75% of their damage to
#: nearby allies as well as enemies".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 50 to 75, with Required Tags
#: `Type.AOE.PointBlank`, no Condition, no Scale and no Action (R10). Which
#: blows, which allies and the delivery are the engine's and not the row's.
#: `UCataclysmAreaShareOnAllies::PercentStat` holds the same name, and
#: `TestAShareOfAPointBlankSkillsBlowOnAnAlly` in
#: `tools/tests/test_generate_datatables.py` compares the two.
#:
#: THE GAME ASKS FOR IT WITH THE SKILL'S OWN TAGS,
#: `UCataclysmAreaShareOnAllies::PercentHeldFor`, at the four places a
#: point-blank skill deals its own blow. So the row's Required Tags are what
#: keep it to the point-blank skills: a row without `Type.AOE.PointBlank`
#: would strike allies with every Strike and every Charge, Leap and Recall.
#: A row of any kind but `flat` works on a base of nought, and a row with an
#: action is not a stat line at all.
#:
#: THE BOUND IS THE ENGINE'S: above nought and at most 100, which
#: `PercentHeldFor` holds the figure to.
AOE_SKILL_DAMAGE_TO_NEARBY_ALLIES_PERCENT_STAT = (
    "aoe_skill_damage_to_nearby_allies_percent")
AOE_SKILL_DAMAGE_TO_NEARBY_ALLIES_REQUIRED_TAG = "Type.AOE.PointBlank"
MAX_AOE_SKILL_DAMAGE_TO_NEARBY_ALLIES_PERCENT = 100.0


def refuse_a_point_blank_share_on_allies_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `AOE_SKILL_DAMAGE_TO_NEARBY_ALLIES_PERCENT_STAT` is a figure
    its one reader takes as it stands, for the point-blank skills only.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constant above
    gives. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_share_of_allies_damage_row_the_game_cannot_read`, for one
    stat, except that this one REQUIRES a tag where that one refuses any.

    MORE TAGS BESIDE `Type.AOE.PointBlank` ARE ALLOWED: they narrow the row to
    fewer of those skills, which the game reads as written.

    A RANGE IS ALLOWED AND MUST RISE: the game rolls from the first column to
    the second.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE, as the delayed damage check leaves one:
    the ruled sentence is an enchantment's.
    """
    problems = []
    stat = AOE_SKILL_DAMAGE_TO_NEARBY_ALLIES_PERCENT_STAT
    needed = AOE_SKILL_DAMAGE_TO_NEARBY_ALLIES_REQUIRED_TAG
    top = MAX_AOE_SKILL_DAMAGE_TO_NEARBY_ALLIES_PERCENT
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; "
                f"any other kind works on a base of nought and strikes NO "
                f"ally.")
        tags = [tag.strip() for tag in
                str(row.get("RequiredTags") or "").split(",") if tag.strip()]
        if needed not in tags:
            problems.append(
                f"{who}: states {stat!r} without the required tag {needed!r}. "
                f"The game asks for this stat with the skill's own tags at "
                f"every Strike swing and every Charge, Leap and Recall, so "
                f"without it the row would strike allies with skills that are "
                f"not point blank.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the game rolls from the first column to "
                f"the second.")
        for value in sorted({low, high}):
            if not 0.0 < value <= top:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the figure "
                    f"must be above 0 and at most {top:g}. Nought or less "
                    f"strikes no ally, and the game holds the figure to "
                    f"{top:g}.")
    return problems


#: The flag by which a slot's key places a skill gadget instead of casting the
#: slot's skill. Ruled 2026-10-16 by the coordinating session (S12), for the
#: rows "Your basic attack / heavy attack / special ability / support ability
#: / ultimate ability is converted into a deployable gadget ... Scales off
#: gadget damage."
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 1 to 1, whose Required Tags is the slot's
#: own tag and nothing else, with no Condition, no Scale and no Action. The
#: sentence's own tags carry `Type.Deployable` as well; the row requires the
#: slot tag alone, because the game asks with the slot's skill tags and a
#: player's skill carries no `Type.Deployable`.
#: `UCataclysmAbilitySystemComponent::SkillSlotIsAGadgetStat` holds the same
#: name, and `TestASlotIsConvertedIntoAGadget` in
#: `tools/tests/test_generate_datatables.py` compares the two.
#:
#: THE FOUR KEYED SLOTS AND THE BASIC ATTACK'S. `Slot.Basic` joined on
#: 2026-10-16 with the second stack, which built its press: a left click on
#: an enemy places or moves the basic attack's gadget (S15), in
#: `ACataclysmPlayerController::PressOnAnEnemy`. Its sentence carries
#: `Type.Deployable, Slot.Basic` (and `Scope.BasicOnly` until #1620 removed
#: the scope labels); the row requires `Slot.Basic` alone, for the reason above.
SKILL_SLOT_IS_A_GADGET_STAT = "skill_slot_is_a_gadget"
SKILL_SLOT_IS_A_GADGET_SLOT_TAGS = frozenset({
    "Slot.Heavy", "Slot.Special", "Slot.Support", "Slot.Ultimate",
    "Slot.Basic"})


def refuse_a_skill_slot_gadget_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `SKILL_SLOT_IS_A_GADGET_STAT` is one its reader answers yes to,
    for one slot.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE. The game asks for the stat with
    the held skill's tags and the slot's tag, and reads any value above nought
    as yes. A row of any kind but `flat` works on a base of nought and says
    no; a row with an action is not a stat line; a row with no required tag
    would convert every slot at once, and one requiring anything but one slot
    tag converts a slot nobody named or none. Each refusal names what the
    reader would do.

    THE FIGURE IS 1 AND NOTHING ELSE, as the other flags hold theirs.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentences are enchantments'.
    """
    problems = []
    stat = SKILL_SLOT_IS_A_GADGET_STAT
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself and takes any value above "
                f"nought as yes, so the row must be 'flat'; any other kind "
                f"works on a base of nought and converts NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags not in SKILL_SLOT_IS_A_GADGET_SLOT_TAGS:
            problems.append(
                f"{who}: states {stat!r} under the required tags {tags!r}. "
                f"The row requires ONE slot's tag and nothing else, one of "
                f"{sorted(SKILL_SLOT_IS_A_GADGET_SLOT_TAGS)}: the game asks "
                f"with each slot's skill tags, so no tag converts every slot "
                f"and any other tag converts a slot nobody named, or none.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The flag "
                f"is a stat line the game asks for, and an action row is not "
                f"one.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if value != 1.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row on this "
                    f"flag states 1 and nothing else.")
    return problems


#: The per cent chance the Null Emperor ten-piece's aura consumes an enemy's
#: shot in flight. Ruled 2026-10-17 by the coordinating session (O9), for
#: "While you have at least 10 stacks of Void, you gain an aura of void energy
#: that has a 50% chance to consume enemy projectiles".
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 50 to 50, with no Required Tags, no
#: Condition, no Scale and no Action. The ten Void, the 3 m and the once-only
#: roll are the engine's and not the row's: `UCataclysmVoidAura` asks the stat
#: only while its wearer holds ten stacks named Void.
#: `UCataclysmVoidAura::ConsumeChanceStat` holds the same name, and
#: `tools/tests/test_null_emperor_void_aura_rows.py` compares the two.
#:
#: THE GAME ASKS FOR IT IN ONE PLACE, WITH NO TAGS,
#: `UCataclysmVoidAura::ConsumeChanceOf`, for a shot in flight, where no skill
#: of the wearer's is in hand. So a row requiring a tag matches nowhere, a row
#: of any kind but `flat` works on a base of nought, and a row with an action is
#: not a stat line at all.
#:
#: THE BOUND IS THE ENGINE'S: above nought and at most 100, which
#: `ConsumeChanceOf` holds the figure to.
VOID_AURA_PROJECTILE_CONSUME_CHANCE_STAT = "void_aura_projectile_consume_chance"
MAX_VOID_AURA_PROJECTILE_CONSUME_CHANCE = 100.0


def refuse_a_void_aura_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `VOID_AURA_PROJECTILE_CONSUME_CHANCE_STAT` is a chance its one
    reader takes as it stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constant above
    gives. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_share_of_allies_damage_row_the_game_cannot_read`, for one
    stat.

    A RANGE IS ALLOWED AND MUST RISE: the game rolls from the first column to
    the second.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    stat = VOID_AURA_PROJECTILE_CONSUME_CHANCE_STAT
    top = MAX_VOID_AURA_PROJECTILE_CONSUME_CHANCE
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the chance itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and consumes NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, for a shot in flight, "
                f"so a row requiring a tag matches nowhere: this one would "
                f"consume NOTHING and say nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the game rolls from the first column to "
                f"the second.")
        for value in sorted({low, high}):
            if not 0.0 < value <= top:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the chance "
                    f"must be above 0 and at most {top:g}. Nought or less "
                    f"consumes nothing, and the game holds the chance to "
                    f"{top:g}.")
    return problems


#: The scale that counts enemies in reach only while another row's own stack is
#: held. Ruled by the project owner on 2026-10-17 for Warlord's Will's ten-piece
#: bonus. See its entry in `SCALES`.
WINDOW_SCALE = "enemies_in_reach_while_own_stack"


def refuse_a_window_scale_with_no_own_stack_row(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row scaled by `WINDOW_SCALE` has a sibling that opens its window.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE. It grants no stack of its own: it
    reads the stack counted under its enchantment and its stat
    (`UCataclysmItemModifiers::OwnStackKeyFor`), which only an `own_stacks` row
    of the same enchantment on the same stat grants. With no such row nothing
    ever grants that stack, the window never opens, and the row counts nobody
    and says nothing.

    AND IT STATES A CAP. The owner's ruling caps the count at 5 enemies, and
    the count it takes, `enemies_in_reach`'s, is uncapped by the owner's ruling
    of 2026-09-12, so a row stating none would be uncapped.
    `SCALES_THAT_NEED_A_CAP` is not widened for it: a merged test holds that
    set to the one scale it names.

    A PASSIVE NODE'S ROW IS LEFT ALONE HERE: `passive_effects` refuses the
    scale outright.
    """
    problems = []
    openers = {(str(row.get("Enchantment") or ""), str(row.get("Stat") or ""))
               for row in rows
               if str(row.get("Scale") or "").strip().lower() == "own_stacks"
               and not str(row.get("Action") or "").strip()}
    for row in rows:
        if str(row.get("Scale") or "").strip().lower() != WINDOW_SCALE \
                or "Node" in row:
            continue
        pair = (str(row.get("Enchantment") or ""), str(row.get("Stat") or ""))
        if pair not in openers:
            problems.append(
                f"{sheet}/{row['Name']}: scales {pair[1]!r} by "
                f"{WINDOW_SCALE!r}, which counts the enemies in reach only "
                f"while a stack of {pair[0]}'s own on {pair[1]!r} is held, and "
                f"no own_stacks row of {pair[0]} on {pair[1]!r} grants one. "
                f"The row would count nobody and grant NOTHING. Write the "
                f"own_stacks row on the same stat beside it.")
        if int(float(row.get("ScaleMaxSteps") or 0)) < 1:
            problems.append(
                f"{sheet}/{row['Name']}: scales {pair[1]!r} by "
                f"{WINDOW_SCALE!r} and states no Scale Max Steps. The count "
                f"has no cap of its own, so the row would be uncapped. State "
                f"the cap: \"capped at 5 enemies\" is 5.")
    return problems


#: The event a kill raises, and the clock a kill stamps.
KILL_EVENT = "kill"
KILL_CLOCK = "seconds_after_kill"

#: The event a spend of Fervour raises, the clock it stamps, and the most an
#: Event Value on it may ask to be spent at once. The Death trees' slice C3a.
SPENT_EVENT = "fervour_spent"
SPENT_CLOCK = "seconds_after_fervour_spent"
MAX_FERVOUR_SPENT_AT_ONCE = 1000.0

#: Each event whose own clock is stamped before its rows act, with that clock.
EVENTS_STAMPING_THEIR_CLOCK_FIRST = {KILL_EVENT: KILL_CLOCK, SPENT_EVENT: SPENT_CLOCK}


def refuse_a_kill_clock_on_a_kill_action(sheet: str,
                                         rows: list[dict]) -> list[str]:
    """No action hung on an event asks the clock that event stamps first:
    `kill` and `seconds_after_kill`, and since the Death trees' slice C3a
    (2026-11-25) `fervour_spent` and `seconds_after_fervour_spent`, whose stamp
    `UCataclysmAbilitySystemComponent::NoteFervourSpent` writes before it raises
    the event, for the same reason.

    IT WOULD ALWAYS HOLD. The kill handler (`ACataclysmPlayerCharacter::
    OnSomethingDied`) pays the kill's Fervour, then stamps the kill clock, then
    raises `kill`, so every row acting on that event reads a clock stamped by
    the same kill: nought seconds, inside any window. A row written as "within
    2 seconds of your previous kill" would act on every kill. Ruled by the
    coordinating session on 2026-11-25 (the Death trees' slice C), with the
    order that makes the condition mean something on a kill's own STAT: the
    payment is asked before the stamp, so `seconds_after_kill` on
    `fervour_per_kill` reads the time since the previous kill.

    EITHER CONDITION COLUMN, because a second condition is judged the same way.
    """
    problems = []
    for row in rows:
        if not str(row.get("Action") or "").strip():
            continue
        event = str(row.get("ActionEvent") or "").strip().lower()
        clock = EVENTS_STAMPING_THEIR_CLOCK_FIRST.get(event)
        if clock is None:
            continue
        for column in ("Condition", "Condition2"):
            if str(row.get(column) or "").strip().lower() == clock:
                problems.append(
                    f"{sheet}/{row['Name']}: acts on {event!r} under "
                    f"{clock!r} ({column}). The clock is stamped by the same "
                    f"event before its rows act, so the window reads nought and "
                    f"always holds: the row would act EVERY time. A kill's own "
                    f"stat, such as fervour_per_kill, may ask the kill clock: "
                    f"that is asked before the stamp and reads the time since "
                    f"the previous kill.")
    return problems


#: Tyrant's Chains' six-piece bonus. Ruled 2026-10-17 by the project owner
#: (#3: the Tyrant's Minion's own death explodes but summons none; at most one
#: stands; a new summon refreshes the one standing) and 2026-10-18 by the
#: coordinating session (Q1-Q5), for "When a pet or minion dies, it explodes,
#: dealing 50% of its maximum health as Death damage to all nearby enemies.
#: Additionally, a new, powerful Tyrant's Minion is summoned to fight for you
#: for a short duration".
#:
#: FOUR ROWS, EACH `flat` WITH NO REQUIRED TAGS, NO CONDITION, NO SCALE AND NO
#: ACTION: Shared Ruin's two stats at 50 and 5 ("nearby" is 5 m), the flag
#: below at 1, and the seconds below at 10 (a number under the owner's
#: delegation). `ACataclysmMinion::DeathBlastDealsDeathStat` and
#: `ACataclysmMinion::TyrantsMinionSecondsStat` hold the same names, and
#: `tools/tests/test_tyrants_chains_six_rows.py` compares the two.
#:
#: THE GAME ASKS FOR BOTH IN ONE PLACE, WITH NO TAGS: off the commander, through
#: `SummonerStat` in `CataclysmMinion.cpp`, when a minion or thrall of its dies.
#: So a row requiring a tag matches nowhere, a row of any kind but `flat` works
#: on a base of nought, and a row with an action is not a stat line at all.
TYRANTS_DEATH_BLAST_DEALS_DEATH_STAT = "minion_death_blast_deals_death"
TYRANTS_MINION_SECONDS_STAT = "minion_death_summons_tyrants_minion_seconds"


def refuse_a_tyrants_minion_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `TYRANTS_DEATH_BLAST_DEALS_DEATH_STAT` or
    `TYRANTS_MINION_SECONDS_STAT` is a figure its one reader takes as it stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constants above
    give. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_void_aura_row_the_game_cannot_read`, for two stats.

    THE FLAG STATES 1 AND NOTHING ELSE, as every flag row does. THE SECONDS MAY
    BE A RANGE, WHICH MUST RISE, and each end must be above nought: nought
    summons nothing, which is what not wearing the row already does.

    A CONDITION OR A SCALE ON EITHER IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    neither stat is on the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    flag = TYRANTS_DEATH_BLAST_DEALS_DEATH_STAT
    seconds = TYRANTS_MINION_SECONDS_STAT
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in (flag, seconds) or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        nothing = ("deals NO Death damage" if stat == flag
                   else "summons NO Tyrant's Minion")
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and {nothing}.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat off the commander with no tags, so "
                f"a row requiring a tag matches nowhere: this one {nothing} "
                f"and says nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if stat == flag:
            for value in sorted({low, high}):
                if value != 1.0:
                    problems.append(
                        f"{who}: states {value:g} on {stat!r}, and a row on "
                        f"this flag states 1 and nothing else.")
            continue
        if low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the game rolls from the first column to "
                f"the second.")
        for value in sorted({low, high}):
            if value <= 0.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the seconds "
                    f"must be above 0: nought or less summons nothing.")
    return problems


#: The Demon King's Regalia's ten-piece bonus. Ruled 2026-10-17 by the
#: coordinating session (the window to make the kill is the 3 s of immunity;
#: "Rare or higher" is Elite and above) and 2026-10-18 (Q1-Q7), for "If you
#: would be killed during Demonic Rage, instead gain 100% crit chance and crit
#: multiplier and become immune to all damage for 3 seconds. Killing a Rare or
#: higher enemy instanly heals you to full, failing to results in your death".
#:
#: THREE STATS, ALL `flat`, WITH NO REQUIRED TAGS AND NO ACTION: the interval
#: between reprieves, the seconds of no damage (and of the window to make the
#: kill), and the lowest rarity rung of a kill that lifts the death sentence.
#: `UCataclysmAbilitySystemComponent::LethalHitReprieveEverySecondsStat`,
#: `LethalHitReprieveSecondsStat` and `LethalHitReprieveKillRarityStat` hold the
#: same names, and `tools/tests/test_demon_kings_regalia_ten_rows.py` compares
#: them.
#:
#: THE GAME ASKS FOR ALL THREE IN ONE PLACE, WITH NO TAGS:
#: `LethalHitReprieveFigure`, when a blow or a tick would kill. So a row
#: requiring a tag matches nowhere, and a row of any kind but `flat` works on a
#: base of nought. ONLY THE INTERVAL MAY BE SCALED, AND ONLY BY ITS OWN STACKS:
#: that is what holds the reprieve to the Demonic Rage, and a reprieve held under
#: any other count is no sentence anybody wrote. THE RUNG IS ONE WHOLE RUNG OF
#: `game/Data/EnemyRarities.csv` FROM ELITE (1) TO CATACLYSM BOSS (5), not a
#: range: a rolled rarity reads as nothing a player could be told.
LETHAL_HIT_REPRIEVE_EVERY_STAT = "lethal_hit_reprieve_every_seconds"
LETHAL_HIT_REPRIEVE_SECONDS_STAT = "lethal_hit_reprieve_seconds"
LETHAL_HIT_REPRIEVE_RARITY_STAT = "lethal_hit_reprieve_kill_rarity_at_least"
LETHAL_HIT_REPRIEVE_STATS = (LETHAL_HIT_REPRIEVE_EVERY_STAT,
                             LETHAL_HIT_REPRIEVE_SECONDS_STAT,
                             LETHAL_HIT_REPRIEVE_RARITY_STAT)

#: The highest rung a kill may be asked to reach: Cataclysm Boss.
LETHAL_HIT_REPRIEVE_HIGHEST_RUNG = 5


def refuse_a_lethal_hit_reprieve_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of `LETHAL_HIT_REPRIEVE_STATS` is a figure its one reader
    takes as it stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constants above
    give. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_tyrants_minion_row_the_game_cannot_read`, for three stats.

    THE INTERVAL AND THE SECONDS MAY BE A RANGE, WHICH MUST RISE, and each end
    must be above nought. THE RUNG IS ONE WHOLE NUMBER FROM 1 TO 5 IN BOTH
    COLUMNS.

    A CONDITION, OR A SCALE ON THE SECONDS OR THE RUNG, IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in LETHAL_HIT_REPRIEVE_STATS or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and reprieves nothing.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags when a blow would kill, "
                f"so a row requiring a tag matches nowhere and says nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        scale = str(row.get("Scale") or "").strip()
        if (stat == LETHAL_HIT_REPRIEVE_EVERY_STAT and scale
                and scale.lower() != "own_stacks"):
            problems.append(
                f"{who}: scales {stat!r} by {scale!r}. The reprieve is held to "
                f"a window by its own stacks and by nothing else: scale it by "
                f"'own_stacks' on the window's event, or not at all.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if stat == LETHAL_HIT_REPRIEVE_RARITY_STAT:
            for value in sorted({low, high}):
                if (value != int(value) or value < 1
                        or value > LETHAL_HIT_REPRIEVE_HIGHEST_RUNG):
                    problems.append(
                        f"{who}: states {value:g} on {stat!r}, and the rung "
                        f"is one whole rung of game/Data/EnemyRarities.csv "
                        f"from 1 (Elite) to "
                        f"{LETHAL_HIT_REPRIEVE_HIGHEST_RUNG} (Cataclysm Boss).")
            if low != high:
                problems.append(
                    f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                    f"rung is one rung in both columns, not a range.")
            continue
        if low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the game rolls from the first column to "
                f"the second.")
        for value in sorted({low, high}):
            if value <= 0.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the seconds "
                    f"must be above 0: nought or less reprieves nothing.")
    return problems


#: Brute's Heart's ten-piece bonus, the Ground Pound. Ruled 2026-10-17 by the
#: project owner (#5: it fires by itself once ready and an enemy is inside its
#: radius, with no key) and 2026-10-18 by the coordinating session (Q1-Q5), for
#: "Every 30 seconds, you can unleash a powerful "Ground Pound" that deals 500%
#: of your armor as damage to all enemies in a large radius and stuns them for
#: 3 seconds. The cooldown of this ability is reduced by 0.5 seconds per kill".
#:
#: FIVE ROWS, EACH `flat` WITH NO REQUIRED TAGS, NO CONDITION, NO SCALE AND NO
#: ACTION, one on each stat below in its order: 30, 500, 8 (a "large radius",
#: a number under the owner's delegation, Q1), 3 and 0.5.
#: `UCataclysmGroundPound` holds the same five names, and
#: `tools/tests/test_brutes_heart_ten_rows.py` compares them.
#:
#: THE GAME ASKS FOR EACH WITH NO TAGS, off the wearer, through
#: `UCataclysmGroundPound::StatOf`, on the regeneration step and at a kill. So a
#: row requiring a tag matches nowhere, a row of any kind but `flat` works on a
#: base of nought, and a row with an action is not a stat line at all.
GROUND_POUND_EVERY_SECONDS_STAT = "ground_pound_every_seconds"
GROUND_POUND_ARMOUR_PERCENT_STAT = "ground_pound_percent_of_armor"
GROUND_POUND_RADIUS_METRES_STAT = "ground_pound_radius_metres"
GROUND_POUND_STUN_SECONDS_STAT = "ground_pound_stun_seconds"
GROUND_POUND_SECONDS_OFF_PER_KILL_STAT = "ground_pound_seconds_off_per_kill"
GROUND_POUND_STATS = (
    GROUND_POUND_EVERY_SECONDS_STAT,
    GROUND_POUND_ARMOUR_PERCENT_STAT,
    GROUND_POUND_RADIUS_METRES_STAT,
    GROUND_POUND_STUN_SECONDS_STAT,
    GROUND_POUND_SECONDS_OFF_PER_KILL_STAT,
)

#: The most each may state, where a bound exists already: the period of a
#: timed row, the share `smite_nearby_by_armor` may state (the blow is that
#: action's), and the longest status. The radius and the seconds a kill takes
#: off have no bound but above nought.
MAX_GROUND_POUND = {
    GROUND_POUND_EVERY_SECONDS_STAT: MAX_EVERY_SECONDS,
    GROUND_POUND_ARMOUR_PERCENT_STAT: MAX_NEARBY_PERCENT["smite_nearby_by_armor"],
    GROUND_POUND_STUN_SECONDS_STAT: MAX_STATUS_SECONDS,
}


def refuse_a_ground_pound_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of `GROUND_POUND_STATS` is a figure its one reader takes as
    it stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constants above
    give. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_tyrants_minion_row_the_game_cannot_read`, for five stats.

    EVERY FIGURE IS ABOVE NOUGHT, and at most its bound where it has one: at
    nought the pound is not armed, strikes nothing, stuns nothing or takes
    nothing off, which is what not wearing the row already does.

    THE SECONDS FROM ONE POUND TO THE NEXT MAY BE A RANGE ONLY IF IT FALLS, as
    `gadget_volley_every_seconds` must: a roll of 1 gives the second figure,
    and the best roll is the shortest wait. Every other range must rise.

    A CONDITION OR A SCALE ON ANY OF THEM IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    none is on the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in GROUND_POUND_STATS or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and the Ground Pound "
                f"gets nothing from it.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat off the wearer with no tags, so a "
                f"row requiring a tag matches nowhere and says nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if stat == GROUND_POUND_EVERY_SECONDS_STAT and low < high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the pair "
                f"must not rise. A roll of 1 gives the second figure, and the "
                f"best roll is the SHORTEST wait: mark the sentence's range in "
                f"the Rolls Down column of the Enchantments sheet.")
        elif stat != GROUND_POUND_EVERY_SECONDS_STAT and low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the game rolls from the first column to "
                f"the second.")
        bound = MAX_GROUND_POUND.get(stat)
        for value in sorted({low, high}):
            if value <= 0.0 or (bound is not None and value > bound):
                most = f" and at most {bound:g}" if bound is not None else ""
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the figure "
                    f"must be above 0{most}: at nought the Ground Pound gets "
                    f"nothing from the row.")
    return problems


#: Warlord's Will's six-piece bonus, the Warhound. Ruled 2026-10-17 by the
#: project owner (#6: the Warhound's hits place a 3 s taunt status, crowd
#: control, shortened by resistance, bosses immune) and 2026-10-18 by the
#: coordinating session (Q1-Q5), for "When you take damage, you have a 10%
#: chance to summon a "Warhound" to fight for you for a short duration. The
#: Warhound taunts enemies and deals damage based on your armor".
#:
#: FOUR ROWS, EACH `flat` WITH NO REQUIRED TAGS, NO CONDITION, NO SCALE AND NO
#: ACTION, one on each stat below in its order: 10 (the per cent chance a
#: landed blow taken summons one), 10 (its seconds, Q3, a number under the
#: owner's delegation), 20 (the per cent of the wearer's armour each hit deals,
#: Q5, a number under the owner's delegation) and 3 (the taunt's seconds, #6).
#: `ACataclysmMinion` holds the same four names, and
#: `tools/tests/test_warlords_will_six_rows.py` compares them.
#:
#: THE GAME ASKS FOR EACH WITH NO TAGS, off the wearer, through `SummonerStat`
#: in `CataclysmMinion.cpp`: the chance where a blow is taken, the seconds at
#: the summon, the share and the taunt at each of the Warhound's hits. So a row
#: requiring a tag matches nowhere, a row of any kind but `flat` works on a base
#: of nought, and a row with an action is not a stat line at all.
WARHOUND_CHANCE_STAT = "warhound_summon_chance"
WARHOUND_SECONDS_STAT = "warhound_seconds"
WARHOUND_ARMOUR_PERCENT_STAT = "warhound_percent_of_armor"
WARHOUND_TAUNT_SECONDS_STAT = "warhound_taunt_seconds"
WARHOUND_STATS = (
    WARHOUND_CHANCE_STAT,
    WARHOUND_SECONDS_STAT,
    WARHOUND_ARMOUR_PERCENT_STAT,
    WARHOUND_TAUNT_SECONDS_STAT,
)

#: The most each may state, where a bound exists already: a chance is at most
#: 100, the share the armour nova may state (the Warhound's hit is a share of
#: armour as that action's is), and the longest status. The Warhound's own
#: seconds have no bound but above nought, as the Tyrant's Minion's have none.
MAX_WARHOUND = {
    WARHOUND_CHANCE_STAT: 100.0,
    WARHOUND_ARMOUR_PERCENT_STAT: MAX_NEARBY_PERCENT["smite_nearby_by_armor"],
    WARHOUND_TAUNT_SECONDS_STAT: MAX_STATUS_SECONDS,
}


def refuse_a_warhound_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of `WARHOUND_STATS` is a figure its one reader takes as it
    stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constants above
    give. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_ground_pound_row_the_game_cannot_read`, for four stats.

    EVERY FIGURE IS ABOVE NOUGHT, and at most its bound where it has one: at
    nought no Warhound comes, its hit deals nothing or taunts nobody, which is
    what not wearing the row already does. EVERY RANGE MUST RISE.

    A CONDITION OR A SCALE ON ANY OF THEM IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    none is on the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in WARHOUND_STATS or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and the Warhound gets "
                f"nothing from it.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat off the wearer with no tags, so a "
                f"row requiring a tag matches nowhere and says nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the game rolls from the first column to "
                f"the second.")
        bound = MAX_WARHOUND.get(stat)
        for value in sorted({low, high}):
            if value <= 0.0 or (bound is not None and value > bound):
                most = f" and at most {bound:g}" if bound is not None else ""
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the figure "
                    f"must be above 0{most}: at nought the Warhound gets "
                    f"nothing from the row.")
    return problems


#: Brute's Heart's six-piece bonus, the taunt aura. Ruled 2026-10-17 by the
#: project owner (#6: Warlord's Will 6's taunt status "also completes Brute's
#: Heart 6's taunt") and 2026-10-18 by the coordinating session (Q1-Q4), for
#: "When your health falls below 50%, you gain a powerful aura that taunts all
#: nearby enemies and increases your armor and resistances by 50%".
#:
#: FOUR ROWS, EACH `flat` WITH NO REQUIRED TAGS, NO CONDITION, NO SCALE AND NO
#: ACTION, one on each stat below in its order: 50 (the share of maximum health
#: the wearer must be strictly below, the sentence's), 1 (the seconds from one
#: beat to the next, Q2), 5 (the metres an enemy is taunted from, Q1) and 3 (the
#: taunt's seconds, Q2). 1 and 5 are numbers under the owner's delegation.
#: `UCataclysmTauntAura` holds the same four names, and
#: `tools/tests/test_brutes_heart_six_rows.py` compares them. The set's armour
#: row, `armor` increased 50 under `health_below` 50, is not one of these.
#:
#: THE GAME ASKS FOR EACH WITH NO TAGS, off the wearer, through
#: `UCataclysmTauntAura::StatOf` on the regeneration step. So a row requiring a
#: tag matches nowhere, a row of any kind but `flat` works on a base of nought,
#: and a row with an action is not a stat line at all. THE SHARE IS A STAT AND
#: NOT A CONDITION ON THE OTHER THREE: the game judges it with the pipeline's own
#: `health_below` predicate, so a condition on any of the four is refused by
#: `refuse_a_condition_nothing_asks_for`, as on every stat it does not list.
TAUNT_AURA_BELOW_HEALTH_PERCENT_STAT = "taunt_aura_below_health_percent"
TAUNT_AURA_EVERY_SECONDS_STAT = "taunt_aura_every_seconds"
TAUNT_AURA_RADIUS_METRES_STAT = "taunt_aura_radius_metres"
TAUNT_AURA_TAUNT_SECONDS_STAT = "taunt_aura_taunt_seconds"
TAUNT_AURA_STATS = (
    TAUNT_AURA_BELOW_HEALTH_PERCENT_STAT,
    TAUNT_AURA_EVERY_SECONDS_STAT,
    TAUNT_AURA_RADIUS_METRES_STAT,
    TAUNT_AURA_TAUNT_SECONDS_STAT,
)

#: The most each may state, where a bound exists already: a share of health is
#: at most 100, the period of a timed row, and the longest status. The radius
#: has no bound but above nought, as the Ground Pound's has none.
MAX_TAUNT_AURA = {
    TAUNT_AURA_BELOW_HEALTH_PERCENT_STAT: 100.0,
    TAUNT_AURA_EVERY_SECONDS_STAT: MAX_EVERY_SECONDS,
    TAUNT_AURA_TAUNT_SECONDS_STAT: MAX_STATUS_SECONDS,
}


def refuse_a_taunt_aura_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of `TAUNT_AURA_STATS` is a figure its one reader takes as
    it stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constants above
    give. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_ground_pound_row_the_game_cannot_read`, for four stats.

    EVERY FIGURE IS ABOVE NOUGHT, and at most its bound where it has one: at
    nought the aura is not armed, which is what not wearing the row already
    does.

    THE SECONDS FROM ONE BEAT TO THE NEXT MAY BE A RANGE ONLY IF IT FALLS, as
    `ground_pound_every_seconds` must: a roll of 1 gives the second figure, and
    the best roll is the shortest wait. Every other range must rise.

    A CONDITION OR A SCALE ON ANY OF THEM IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    none is on the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in TAUNT_AURA_STATS or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and the taunt aura gets "
                f"nothing from it.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat off the wearer with no tags, so a "
                f"row requiring a tag matches nowhere and says nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if stat == TAUNT_AURA_EVERY_SECONDS_STAT and low < high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the pair "
                f"must not rise. A roll of 1 gives the second figure, and the "
                f"best roll is the SHORTEST wait: mark the sentence's range in "
                f"the Rolls Down column of the Enchantments sheet.")
        elif stat != TAUNT_AURA_EVERY_SECONDS_STAT and low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the game rolls from the first column to "
                f"the second.")
        bound = MAX_TAUNT_AURA.get(stat)
        for value in sorted({low, high}):
            if value <= 0.0 or (bound is not None and value > bound):
                most = f" and at most {bound:g}" if bound is not None else ""
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the figure "
                    f"must be above 0{most}: at nought the taunt aura gets "
                    f"nothing from the row.")
    return problems


#: Tyrant's Chains' ten-piece bonus. Ruled 2026-10-17 by the coordinating
#: session (#7: the Tyrant's Minion's ultimate cast is a free start, as the
#: skill gadget's) and 2026-10-18 (T1-T7), for "Your Tyrant's Minion now casts
#: your ultimate ability every 3 seconds".
#:
#: ONE ROW, `flat` WITH NO REQUIRED TAGS, NO CONDITION, NO SCALE AND NO ACTION:
#: the seconds between casts at 3, the sentence's own number.
#: `ACataclysmMinion::TyrantsMinionCastsUltimateEverySecondsStat` holds the same
#: name, and `tools/tests/test_tyrants_chains_ten_rows.py` compares the two.
#:
#: THE GAME ASKS FOR IT IN ONE PLACE, WITH NO TAGS: off the commander, through
#: `SummonerStat` in `CataclysmMinion.cpp`, from
#: `ACataclysmMinion::TyrantsUltimateSecondsBetweenCasts`. So a row requiring a
#: tag matches nowhere, a row of any kind but `flat` works on a base of nought,
#: and a row with an action is not a stat line at all. A WAIT, SO LOWER IS
#: BETTER, and a range of it must not rise, as the taunt aura's interval must
#: not (the owner's rule of 2026-10-05: a lower-is-better row rolls low as the
#: best roll).
TYRANTS_MINION_CASTS_ULTIMATE_STAT = "tyrants_minion_casts_ultimate_every_seconds"


def refuse_a_tyrants_ultimate_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `TYRANTS_MINION_CASTS_ULTIMATE_STAT` is a figure its one reader
    takes as it stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constant above
    gives. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_taunt_aura_row_the_game_cannot_read`, for one stat.

    THE FIGURE IS ABOVE NOUGHT AND AT MOST `MAX_EVERY_SECONDS`, the period of a
    timed row: at nought the Tyrant's Minion casts nothing, which is what not
    wearing the row already does. A RANGE MUST NOT RISE, as above.

    A CONDITION OR A SCALE ON IT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither list those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    stat_name = TYRANTS_MINION_CASTS_ULTIMATE_STAT
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat != stat_name or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and the Tyrant's Minion "
                f"casts nothing.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat off the commander with no tags, so "
                f"a row requiring a tag matches nowhere: the Tyrant's Minion "
                f"casts nothing and the row says nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if low < high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the pair "
                f"must not rise. A roll of 1 gives the second figure, and the "
                f"best roll is the SHORTEST wait: mark the sentence's range in "
                f"the Rolls Down column of the Enchantments sheet.")
        for value in sorted({low, high}):
            if value <= 0.0 or value > MAX_EVERY_SECONDS:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the seconds "
                    f"must be above 0 and at most {MAX_EVERY_SECONDS:g}: at "
                    f"nought the Tyrant's Minion casts nothing.")
    return problems


#: Reaper's Embrace's six-piece bonus. The owner's ruling #8 of 2026-10-17 (a
#: Soul is picked up by walking within about 1.5 m and lasts the floor) and the
#: coordinating session's rulings R1-R8 of 2026-10-18, for "When a minion or pet
#: dies, it leaves behind a "Soul" that can be picked up. Picking up a Soul
#: grants a stack of Soul Strike that increases your damage by 10% for 5
#: seconds. Max 10 stacks."
#:
#: THREE ROWS: this flag at a flat 1, and Soul Strike as two own-stack rows,
#: `attack_damage` and `spell_damage` increased 10, step 1, on the event
#: `soul_picked_up`, at most 10 steps, 5 s. Only the flag is new; the two
#: Soul Strike rows are the shape `refuse_a_scale_nothing_asks_for` and the
#: action checks already hold. `ACataclysmReaperSoul::MinionDeathLeavesSoulStat`
#: holds the same name, and `tools/tests/test_reapers_embrace_six_rows.py`
#: compares the two.
#:
#: THE GAME ASKS FOR IT IN ONE PLACE, WITH NO TAGS: off the commander, by
#: `ACataclysmReaperSoul::LeaveFor`, when a minion or thrall of its dies. So a
#: row requiring a tag matches nowhere, a row of any kind but `flat` works on a
#: base of nought, and a row with an action is not a stat line at all.
REAPER_SOUL_STAT = "minion_death_leaves_soul"


def refuse_a_reaper_soul_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `REAPER_SOUL_STAT` is a flag its one reader takes as it stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constant above
    gives. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_tyrants_minion_row_the_game_cannot_read`'s flag, for one stat.

    THE FLAG STATES 1 AND NOTHING ELSE, as every flag row does.

    A CONDITION OR A SCALE ON IT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither list those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    flag = REAPER_SOUL_STAT
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat != flag or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and no minion's death "
                f"leaves a Soul.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat off the commander with no tags, so "
                f"a row requiring a tag matches nowhere: no minion's death "
                f"leaves a Soul and the row says nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        for value in sorted({low, high}):
            if value != 1.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row on "
                    f"this flag states 1 and nothing else.")
    return problems


#: Reaper's Embrace's ten-piece bonus. The owner's ruling #9 of 2026-10-17
#: (cull is a threshold of 5% of maximum health per Soul Strike stack, capped
#: by rarity: Common 50%, Elite and Legendary 25%, Herald 10%, never a boss)
#: and the coordinating session's rulings C1-C8 of 2026-10-19, for "For each
#: stack of Soul Strike you gain 5% cull on hit. Killing an enemy instanly
#: spawns one of your minions, up to your max".
#:
#: TWO ROWS: the cull per stack at a flat 5, and the spawn's flag at a flat 1.
#: The stacks are the six-piece's own (`soul_picked_up`), and the caps are the
#: engine's, so neither is a cell. The engine holds the same names in
#: `UCataclysmAbilitySystemComponent::CullPercentPerSoulStrikeStackStat` and
#: `UCataclysmSummonSkill::KillSpawnsMinionStat`, and
#: `tools/tests/test_reapers_embrace_ten_rows.py` compares them.
#:
#: THE GAME ASKS FOR EACH IN ONE PLACE, WITH NO TAGS: the cull off the wearer
#: by `CullSharePercentAgainst` when its hit lands, the flag off the killer by
#: `SpawnOneAtAKill` at its kill. So a row requiring a tag matches nowhere, a
#: row of any kind but `flat` works on a base of nought, and a row with an
#: action is not a stat line at all.
REAPER_CULL_STAT = "cull_percent_per_soul_strike_stack"
REAPER_SPAWN_STAT = "minion_kill_spawns_minion"


def refuse_a_reaper_ten_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `REAPER_CULL_STAT` or `REAPER_SPAWN_STAT` that its reader
    would take as nothing, or as something else.

    THE SHAPE OF `refuse_a_reaper_soul_row_the_game_cannot_read`, for two
    stats. Each refusal names what the reader would do with the row.

    THE CULL STATES A SHARE ABOVE 0 AND AT MOST 100 per stack: at nought
    nothing is culled, and the rarity caps hold every share to 50 at most.
    THE FLAG STATES 1 AND NOTHING ELSE, as every flag row does.

    A CONDITION OR A SCALE ON EITHER IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    readers = {
        REAPER_CULL_STAT: "nothing is culled",
        REAPER_SPAWN_STAT: "no kill spawns a minion",
    }
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in readers or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        nothing = readers[stat]
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and {nothing}.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, so a row requiring a "
                f"tag matches nowhere: {nothing} and the row says nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        for value in sorted({low, high}):
            if stat == REAPER_SPAWN_STAT and value != 1.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row on "
                    f"this flag states 1 and nothing else.")
            if stat == REAPER_CULL_STAT and not 0.0 < value <= 100.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the share of "
                    f"maximum health per stack must be above 0 and at most "
                    f"100: at nought {nothing}.")
    return problems


#: Chronomancer's Time-Lock's ten-piece bonus, the temporal rift. Ruled
#: 2026-10-08 by the project owner (the freeze acts like a stun), 2026-10-17
#: (#10: the rift lengthens 1 s per enemy caught, at most 10 s, and an enemy
#: already caught in it is not frozen again) and 2026-10-19 by the coordinating
#: session (K1-K9), for "Every 30 seconds, a "temporal rift" appears around you.
#: All enemies that enter the rift are frozen in time for 5 seconds and take
#: 500% more damage from all sources. Every enemy caught in it extends the
#: duration by 1s".
#:
#: SEVEN ROWS, EACH `flat` WITH NO REQUIRED TAGS, NO CONDITION, NO SCALE AND NO
#: ACTION, one on each stat below in its order: 30, 5 (the radius, K3, a number
#: under the owner's delegation), 5 (the base length, K2, the same), 1, 10 (the
#: owner's most, #10, of the WHOLE length, K1), 5 (the stun) and 500.
#: `UCataclysmTimeLockRift` holds the same seven names, and
#: `tools/tests/test_chronomancers_time_lock_ten_rows.py` compares them.
#:
#: THE GAME ASKS FOR EACH WITH NO TAGS, off the wearer, through
#: `UCataclysmTimeLockRift::StatOf` on the regeneration step. So a row requiring
#: a tag matches nowhere, a row of any kind but `flat` works on a base of
#: nought, and a row with an action is not a stat line at all.
TIME_LOCK_RIFT_EVERY_SECONDS_STAT = "time_lock_rift_every_seconds"
TIME_LOCK_RIFT_RADIUS_METRES_STAT = "time_lock_rift_radius_metres"
TIME_LOCK_RIFT_SECONDS_STAT = "time_lock_rift_seconds"
TIME_LOCK_RIFT_SECONDS_PER_ENEMY_STAT = "time_lock_rift_seconds_per_enemy"
TIME_LOCK_RIFT_MOST_SECONDS_STAT = "time_lock_rift_most_seconds"
TIME_LOCK_RIFT_STUN_SECONDS_STAT = "time_lock_rift_stun_seconds"
TIME_LOCK_RIFT_MORE_PERCENT_STAT = "time_lock_rift_damage_taken_more_percent"
TIME_LOCK_RIFT_STATS = (
    TIME_LOCK_RIFT_EVERY_SECONDS_STAT,
    TIME_LOCK_RIFT_RADIUS_METRES_STAT,
    TIME_LOCK_RIFT_SECONDS_STAT,
    TIME_LOCK_RIFT_SECONDS_PER_ENEMY_STAT,
    TIME_LOCK_RIFT_MOST_SECONDS_STAT,
    TIME_LOCK_RIFT_STUN_SECONDS_STAT,
    TIME_LOCK_RIFT_MORE_PERCENT_STAT,
)

#: The most each may state, where a bound exists already: the period of a
#: timed row, and the longest status. The radius, the lengths and the share of
#: more damage have no bound but above nought, as the Ground Pound's radius has
#: none.
MAX_TIME_LOCK_RIFT = {
    TIME_LOCK_RIFT_EVERY_SECONDS_STAT: MAX_EVERY_SECONDS,
    TIME_LOCK_RIFT_STUN_SECONDS_STAT: MAX_STATUS_SECONDS,
}


def refuse_a_time_lock_rift_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of `TIME_LOCK_RIFT_STATS` is a figure its one reader takes
    as it stands.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constants above
    give. Each refusal names what the reader would do with the row. The shape
    of `refuse_a_ground_pound_row_the_game_cannot_read`, for seven stats.

    EVERY FIGURE IS ABOVE NOUGHT, and at most its bound where it has one: at
    nought the rift is not armed, is never placed, does not grow, has no most,
    stuns nothing or adds nothing to what an enemy takes, none of which the
    sentence asks for.

    THE SECONDS FROM ONE RIFT TO THE NEXT MAY BE A RANGE ONLY IF IT FALLS, as
    `ground_pound_every_seconds` must: a roll of 1 gives the second figure, and
    the best roll is the shortest wait. Every other range must rise.

    A CONDITION OR A SCALE ON ANY OF THEM IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    none is on the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in TIME_LOCK_RIFT_STATS or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself, so the row must be 'flat'; any "
                f"other kind works on a base of nought and the temporal rift "
                f"gets nothing from it.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat off the wearer with no tags, so a "
                f"row requiring a tag matches nowhere and says nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The "
                f"stat is a stat line the game asks for, and an action row is "
                f"not one.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if stat == TIME_LOCK_RIFT_EVERY_SECONDS_STAT and low < high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the pair "
                f"must not rise. A roll of 1 gives the second figure, and the "
                f"best roll is the SHORTEST wait: mark the sentence's range in "
                f"the Rolls Down column of the Enchantments sheet.")
        elif stat != TIME_LOCK_RIFT_EVERY_SECONDS_STAT and low > high:
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and the "
                f"range must rise: the game rolls from the first column to "
                f"the second.")
        bound = MAX_TIME_LOCK_RIFT.get(stat)
        for value in sorted({low, high}):
            if value <= 0.0 or (bound is not None and value > bound):
                most = f" and at most {bound:g}" if bound is not None else ""
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and the figure "
                    f"must be above 0{most}: at nought the temporal rift gets "
                    f"nothing from the row.")
    return problems


#: The flag of "You cannot benefit from buffs applied by allies", ruled
#: 2026-10-19 (Q3 option a, A1): held by the RECEIVER, it refuses every buff
#: another character's code gives it.
#:
#: THE GAME ASKS WHETHER IT IS ABOVE NOUGHT, WITH NO TAGS,
#: `UCataclysmAbilitySystemComponent::RefusesBuffsFromAllies`, of each ally
#: the shared buffs' step and an aura's two passes are about to give a buff.
#: So a row requiring a tag matches nowhere, a row of any kind but `flat`
#: works on a base of nought, and an action row is not a stat line.
ALLY_BUFFS_REFUSED_STAT = "ally_buffs_refused"


def refuse_an_ally_buffs_refused_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `ALLY_BUFFS_REFUSED_STAT` is a flag: flat, 1 to 1, with no
    required tag and no action.

    THE ROW IS ACCEPTED AND WRONG OTHERWISE, for the reasons the constant
    above gives. Each refusal names what the reader would do with the row.
    The shape of `refuse_a_debuff_immunity_flag_row_the_game_cannot_read`.

    A FIGURE OTHER THAN 1 IS REFUSED though the game reads any value above
    nought as yes: a flag that states another number reads as a quantity.

    A CONDITION OR A SCALE ON THE FLAG IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the flag is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    stat = ALLY_BUFFS_REFUSED_STAT
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game asks whether the flag is above nought, so the row "
                f"must be 'flat'; any other kind works on a base of nought "
                f"and refuses NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this flag off the receiver with no tags, so a "
                f"row requiring a tag matches nowhere: this one would refuse "
                f"NOTHING and say nothing.")
        low = float(row.get("ValueLow") or 0.0)
        high = float(row.get("ValueHigh") or 0.0)
        if (low, high) != (1.0, 1.0):
            problems.append(
                f"{who}: states {low:g} to {high:g} on {stat!r}, and a flag "
                f"is 1 to 1. The game reads any figure above nought as yes, "
                f"so another figure reads as a quantity and is none.")
        if str(row.get("Action") or "").strip():
            problems.append(
                f"{who}: states {stat!r} on a row with the action "
                f"{str(row.get('Action')).strip()!r}. The flag is read as a "
                f"stat line and an action row states none, so this row would "
                f"refuse NOTHING.")
    return problems


#: The flag by which a wearer's own deployable's blast strikes the wearer
#: standing inside it. Ruled 2026-10-19 by the coordinating session (B1-B6),
#: for "Your deployables and traps deal full damage to you if you are within
#: their blast radius", the deployable half; the trap half is not yet built.
#: SINCE TRAPS LAYER 7 (2026-10-22) THE TRAP HALF IS BUILT: a skill trap is a
#: deployable of its wearer's, and a shot, landing or held-back swing of one
#: already used up is found by its identity (`IsTheWearersUsedUpTrap`).
#:
#: ONE SENTENCE IS ONE ROW: `flat`, 1 to 1, with no Required Tags, no
#: Condition, no Scale and no Action (B1). Which blows, who is inside and the
#: figure are the engine's and not the row's.
#: `UCataclysmDeployableSelfBlast::FlagStat` holds the same name, and
#: `TestADeployablesBlastStrikesItsWearer` in
#: `tools/tests/test_generate_datatables.py` compares the two.
#:
#: THE GAME ASKS FOR IT WITH NO TAGS, `UCataclysmDeployableSelfBlast::HeldBy`,
#: at each place a deployable's blast is struck, because a death blast has no
#: skill whose tags could scope it; it reads any value above nought as yes. So
#: a row requiring a tag matches nowhere, a row of any kind but `flat` works on
#: a base of nought and says no, and a row with an action is not a stat line
#: at all; `refuse_a_deployable_self_blast_row_the_game_cannot_read` refuses
#: each.
DEPLOYABLE_BLASTS_STRIKE_SELF_STAT = "deployable_blasts_strike_self"


def refuse_a_deployable_self_blast_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `DEPLOYABLE_BLASTS_STRIKE_SELF_STAT` is one its reader answers
    yes to.

    THE ROW IS ACCEPTED AND DEAD OTHERWISE, for the reasons the constant above
    gives. Each refusal names what the reader would do with the row. The shape
    of `refuse_an_ultimate_cost_doubling_row_the_game_cannot_read`, for one
    flag.

    THE FIGURE IS 1 AND NOTHING ELSE. The reader takes any value above nought
    as yes, so 2 would work in the game; it is refused because a flag that
    states another number reads as a quantity, and "full damage" is not one a
    row may change.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by
    `refuse_a_condition_nothing_asks_for` and `refuse_a_scale_nothing_asks_for`:
    the stat is on neither of the lists those two read.

    A PASSIVE NODE'S ROW IS LEFT ALONE: the ruled sentence is an enchantment's.
    """
    problems = []
    stat = DEPLOYABLE_BLASTS_STRIKE_SELF_STAT
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself and takes any value above "
                f"nought as yes, so the row must be 'flat'; any other kind "
                f"works on a base of nought and strikes NOBODY.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags:
            problems.append(
                f"{who}: states {stat!r} under a required tag, {tags!r}. The "
                f"game asks for this stat with no tags, because a deployable's "
                f"death blast has no skill to scope it, so a row requiring a "
                f"tag matches nowhere: this one would strike NOBODY and say "
                f"nothing.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The flag "
                f"is a stat line the game asks for, and an action row is not "
                f"one.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if value != 1.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row on this "
                    f"flag states 1 and nothing else.")
    return problems


#: The flag by which a slot's press PLACES A SKILL TRAP, which casts the slot's
#: skill once when an enemy comes within 3 m of it. Ruled 2026-10-19 by the
#: coordinating session (T1-T19), for "Your heavy attack / special ability /
#: ultimate ability is converted into a placeable trap. When triggered, it
#: fires your ... Scales off trap damage. All placed copies share the ...
#: cooldown."
#:
#: ONE SENTENCE IS ONE ROW, as the skill gadget's flag: `flat`, 1 to 1, whose
#: Required Tags is the slot's own tag and nothing else, with no Condition, no
#: Scale and no Action. The sentence's own tags carry `Type.Trap` as well; the
#: row requires the slot tag alone, because the game asks with the slot's
#: skill tags. `UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat` holds
#: the same name.
#:
#: THE FIVE SLOTS THE SENTENCES CONVERT. The Heavy, the Special and the
#: Ultimate since traps layer 1; the Support (its self buff, granted within
#: 8 m of the trap, T7) and the basic attack (placed by a click on an enemy,
#: once per swing interval) since traps layer 2, ruled 2026-10-21 (L1-L6),
#: which lifted their refusal.
SKILL_SLOT_IS_A_TRAP_STAT = "skill_slot_is_a_trap"
SKILL_SLOT_IS_A_TRAP_SLOT_TAGS = frozenset({
    "Slot.Heavy", "Slot.Special", "Slot.Ultimate", "Slot.Support",
    "Slot.Basic"})


def refuse_a_skill_slot_trap_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `SKILL_SLOT_IS_A_TRAP_STAT` is one its reader answers yes to,
    for one slot a sentence converts.

    THE SHAPE OF `refuse_a_skill_slot_gadget_row_the_game_cannot_read`, for
    the trap's flag, and for its reasons: a kind but `flat` works on a base of
    nought and converts nothing; no tag converts every slot; a tag but one
    built slot's converts a slot nobody named or one not built; an action is
    not a stat line; the figure is 1.

    A CONDITION OR A SCALE ON THE STAT IS REFUSED ELSEWHERE, by the two checks
    that refuse those on a stat not on their lists. A PASSIVE NODE'S ROW IS
    LEFT ALONE: the ruled sentences are enchantments'.
    """
    problems = []
    stat = SKILL_SLOT_IS_A_TRAP_STAT
    for row in rows:
        if str(row.get("Stat") or "").strip() != stat or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the figure itself and takes any value above "
                f"nought as yes, so the row must be 'flat'; any other kind "
                f"works on a base of nought and converts NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags not in SKILL_SLOT_IS_A_TRAP_SLOT_TAGS:
            problems.append(
                f"{who}: states {stat!r} under the required tags {tags!r}. "
                f"The row requires ONE slot's tag and nothing else, one of "
                f"{sorted(SKILL_SLOT_IS_A_TRAP_SLOT_TAGS)}: the game asks "
                f"with each slot's skill tags, so no tag converts every slot, "
                f"and no other slot has a trap.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. The flag "
                f"is a stat line the game asks for, and an action row is not "
                f"one.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if value != 1.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row on this "
                    f"flag states 1 and nothing else.")
    return problems


#: The two increases a skill trap is read by, ruled 2026-10-19 (M3, M4): how
#: much faster it arms and how much wider it triggers. Each is asked as an
#: INCREASE, with the trap's type tags, by `ACataclysmMinion::
#: SkillTrapArmingSecondsFor` and `SkillTrapTriggerRadiusCmFor`, whose names
#: `TrapArmingSpeedStat` and `TrapTriggerRadiusStat` hold. Their rows are a
#: later layer's: "Traps arm 30%-60% faster" and "Traps have 30%-50% increased
#: trigger radius".
TRAP_INCREASE_STATS = ("trap_arming_speed", "trap_trigger_radius")

#: The Required Tags a row on one of them may state: none, or the trap's own
#: tag. The trap is asked with its type tags, `Type.Minion, Type.Deployable,
#: Minion.Machine, Type.Trap`; a row naming anything outside them matches no
#: trap, and one naming `Type.Deployable` would read as a gadget row.
TRAP_INCREASE_REQUIRED_TAGS = frozenset({"", "Type.Trap"})


def refuse_a_trap_increase_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of `TRAP_INCREASE_STATS` is one its reader adds.

    THE READER ASKS FOR THE INCREASES AND NOTHING ELSE, so a `flat` or a
    `more` row is accepted and dead: refused. A row requiring a tag the trap
    does not carry is dead too, and so is an action row, which is not a stat
    line. A CONDITION OR A SCALE IS REFUSED ELSEWHERE, as on every stat not on
    those checks' lists. A PASSIVE NODE'S ROW IS LEFT ALONE.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in TRAP_INCREASE_STATS or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "increased":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads only the increases of this stat, so the row must "
                f"be 'increased'; any other kind changes NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags not in TRAP_INCREASE_REQUIRED_TAGS:
            problems.append(
                f"{who}: states {stat!r} under the required tags {tags!r}. "
                f"The game asks with the trap's type tags, so the row requires "
                f"nothing or 'Type.Trap'; any other tag matches no trap, or "
                f"reads as a row on gadgets.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. This is "
                f"a stat line the game asks for, and an action row is not "
                f"one.")
    return problems


#: The no-expire flag, ruled 2026-10-21 by the coordinating session (traps
#: layer 3, M2): "Traps do not expire, they remain armed until triggered". It
#: is asked as a FLAT figure off the player, with `Type.Trap` alone, by
#: `ACataclysmMinion::SkillTrapsDoNotExpireFor`, whose name
#: `TrapsDoNotExpireStat` holds; it leaves a trap placed while it is held with
#: no life span (M2 (a)). ITS SIBLING, THE TRAP CAP BONUS (M1), WENT WITH THE
#: TRAP CAP in issue #2787's branch B, when the owner's ruling of 2026-11-20
#: removed every count cap on what reserves the class resource; "Your trap
#: skills get +1-6 to max trap count" became "Your traps reserve 5%-25% reduced
#: Fervour", on `MINION_RESERVE_PERCENT_LESS_STAT` below.
TRAPS_DO_NOT_EXPIRE_STAT = "traps_do_not_expire"

#: The Required Tags a row on it may state: none, or the trap's own tag, the one
#: tag it is asked with.
TRAP_EXPIRY_REQUIRED_TAGS = frozenset({"", "Type.Trap"})


def refuse_a_trap_expiry_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `TRAPS_DO_NOT_EXPIRE_STAT` is one its reader adds.

    IT IS READ AS A FLAT FIGURE, so any other kind works on a base of nought
    and changes NOTHING; a tag but none or `Type.Trap` matches no trap; an
    action is not a stat line. THE FLAG STATES 1 AND NOTHING ELSE. A CONDITION
    OR A SCALE IS REFUSED ELSEWHERE, as on every stat not on those checks'
    lists. A PASSIVE NODE'S ROW IS LEFT ALONE.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat != TRAPS_DO_NOT_EXPIRE_STAT or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the flat figure itself, so the row must be "
                f"'flat'; any other kind works on a base of nought and "
                f"changes NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags not in TRAP_EXPIRY_REQUIRED_TAGS:
            problems.append(
                f"{who}: states {stat!r} under the required tags {tags!r}. "
                f"The game asks with 'Type.Trap' alone, so the row requires "
                f"nothing or 'Type.Trap'; any other tag matches no trap.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. This is "
                f"a stat line the game asks for, and an action row is not "
                f"one.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if value != 1.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, and a row on this "
                    f"flag states 1 and nothing else.")
    return problems


#: How many percent LESS one minion, gadget or trap reserves; a negative
#: figure is that many percent MORE. Issue #2787, branch B, from the owner's
#: ruling of 2026-11-20 that what raised a count cap lowers the reserve instead
#: ("Lower the reserve instead"). Asked as a FLAT figure by
#: `UCataclysmCommand::ReserveFor` with the kind's tags, whose name
#: `MinionReservePercentLessStat` holds, the rows reaching one kind adding into
#: one figure: reserve = max(1, (stated - Crowned) x (1 - net / 100)).
MINION_RESERVE_PERCENT_LESS_STAT = "minion_reserve_percent_less"

#: The Required Tags a row on it may state: none, for every kind ("Your
#: minions"); `Type.Deployable`, a skill gadget and a skill trap ("Your
#: deployables"); or `Type.Trap`, a skill trap alone ("Your traps"). Those are
#: the tags `game/Data/MinionTypes.csv` gives the kinds that reserve.
#:
#: AND `Type.Gadget` SINCE 2026-11-25, the tag for gadgets alone (the
#: coordinating session's ruling): "Your gadgets reserve 10% reduced Fervour"
#: reaches a skill gadget and not a skill trap.
MINION_RESERVE_PERCENT_REQUIRED_TAGS = frozenset({"", "Type.Deployable", "Type.Trap", "Type.Gadget"})

#: The most a row may state either way, in per cent. A bound for typing
#: mistakes, as `MAX_STATUS_SIZE` is: the sentences say 5 to 30 less and 25 to
#: 65 more, and a hundred less is already the floor of one point.
MAX_MINION_RESERVE_PERCENT = 100.0


def refuse_a_reserve_percent_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `MINION_RESERVE_PERCENT_LESS_STAT` is one its reader adds.

    IT IS READ AS A FLAT FIGURE, so any other kind works on a base of nought
    and changes NOTHING; a tag but none, `Type.Deployable` or `Type.Trap`
    matches no kind that reserves; an action is not a stat line. EACH END IS A
    PERCENTAGE OTHER THAN NOUGHT AND NO FURTHER FROM IT THAN
    `MAX_MINION_RESERVE_PERCENT`, AND BOTH ENDS HAVE ONE SIGN: a row is less or
    more, and a roll between a less and a more would cross nought. A CONDITION
    OR A SCALE IS REFUSED ELSEWHERE. A PASSIVE NODE'S ROW IS LEFT ALONE, as the
    trap rows' are.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat != MINION_RESERVE_PERCENT_LESS_STAT or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads the flat figure itself, so the row must be "
                f"'flat'; any other kind works on a base of nought and "
                f"changes NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags not in MINION_RESERVE_PERCENT_REQUIRED_TAGS:
            problems.append(
                f"{who}: states {stat!r} under the required tags {tags!r}. "
                f"The game asks with the tags of the kind that reserves, so "
                f"the row requires nothing, 'Type.Deployable' or 'Type.Trap'; "
                f"any other tag matches no kind that reserves.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. This is "
                f"a stat line the game asks for, and an action row is not "
                f"one.")
        ends = [float(row.get(column) or 0.0) for column in ("ValueLow", "ValueHigh")]
        for value in ends:
            if value == 0.0 or abs(value) > MAX_MINION_RESERVE_PERCENT:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}. A reserve "
                    f"percentage is other than nought and at most "
                    f"{MAX_MINION_RESERVE_PERCENT:g} either way.")
        if ends[0] * ends[1] < 0.0:
            problems.append(
                f"{who}: states {ends[0]:g} to {ends[1]:g} on {stat!r}. A row "
                f"is a reserve that is less or one that is more, so both ends "
                f"have one sign.")
    return problems


#: The action that SLOWS THE OTHER CHARACTER OF ITS EVENT by its value in per
#: cent for its Stack Seconds, rolled with the value when Stack Seconds High is
#: stated. Ruled 2026-10-21 by the coordinating session (traps layer 3, M3
#: (b)): "Traps apply a 20%-35% slow to triggered enemies for 2-4 seconds" is
#: ONLY THE ENEMY THAT SET THE TRAP OFF, the other character of
#: `trap_triggered`, and not every enemy the cast strikes. Cripple's slow at
#: the stated strength, which `apply_status_seconds` cannot state: it lays
#: Cripple at Cripple's own 30 (held 2026-10-07 for that reason).
#: `UCataclysmAbilitySystemComponent::SlowForSecondsAction` holds the name.
#:
#: ON `trap_triggered` AND NO OTHER EVENT, AND UNDER THE OWNER'S RULE OF A
#: TENTH (#917). The owner, 2026-10-21: "Follow the tenth rule" — "The slow
#: lands only if the trap's cast then hits that same enemy for at least a tenth
#: of its maximum health." The event's amount is the largest blow the trap's
#: cast dealt the enemy that set it off (`ACataclysmMinion::
#: SkillTrapTookFromItsEnemy`), and the rule is asked against it, as for every
#: ailment a row lays on another character. AND WHEN NO BLOW HAS YET QUALIFIED,
#: the slow waits up to 5 s for a later blow of the same cast on that enemy (a
#: shot that lands after the trap is used up), and that blow decides it (ruled
#: 2026-10-21, brief 60c). The row's cells are the same either way. No Ailment
#: cell: the slow is always Cripple's.
SLOW_FOR_SECONDS_ACTION = "slow_for_seconds"
SLOW_FOR_SECONDS_EVENTS = ("trap_triggered",)

#: THE ACTIONS THAT READ A `Stack Seconds` CELL, built from the same names the
#: reading branches of `enchantment_effects` test rather than typed again. Issue
#: #2337. A placed stack's window (`PLACED_ACTIONS`), a nearby action's window
#: for its Every Nth count (`NEARBY_ACTIONS`), the stun near the dying's seconds
#: and the slow's seconds. A stat row reads the column only when scaled by
#: `own_stacks`, which is not an action. Every other action drops the cell, so
#: a cell on one is refused. Adding an action that reads the column means adding
#: its name here as well as its branch. The test of this list does NOT catch a
#: missing name: it compares the list with these four sources, so it cannot see a
#: fifth. What fails is the refusal below: the new action's rows that state a cell
#: are refused, so that branch's own tests fail.
ACTIONS_THAT_READ_STACK_SECONDS = frozenset({
    *PLACED_ACTIONS, *NEARBY_ACTIONS, STUN_NEAR_THE_DYING_ACTION,
    SLOW_FOR_SECONDS_ACTION})

#: The strongest slow the action may state, in per cent: Cripple's own cap in
#: `game/Data/StatusEffects.csv`, which the game holds it to.
MAX_SLOW_PERCENT = 80.0

#: The events an `own_stacks` row MAY STATE NO CAP on, and whose stacks then
#: LAPSE ALONE, each its own Stack Seconds after it was granted. Ruled
#: 2026-10-21 by the coordinating session (traps layer 3, M4 (a)): "Each trap
#: triggered in the last 10 seconds increases your damage by 8%-15%" is one
#: stack per trigger, each lasting 10 s, and no cap, since the sentence names
#: none. Everywhere else a cap is still required: a missing one is still a row
#: written wrong. The item reads a cap of nought as
#: `FCataclysmPoolAction::bOwnStacksLapseAlone`.
OWN_STACKS_LAPSING_ALONE_EVENTS = ("trap_triggered",)


def _check_slow_for_seconds_action(index: int, who: str, action: str,
                                   event: str, fraction_of: str, kind: str,
                                   raw, headers: dict[str, int]) -> None:
    """Everything a slow must say, and everything it must not. Ruled
    2026-10-21 (traps layer 3, M3 (b)). The event must be one whose other
    character is the one the sentence slows; the strength is checked where the
    value is read and the seconds where Stack Seconds is. A fraction, a value
    kind and a scale each mean nothing here, so each is refused rather than
    dropped.
    """
    if event not in SLOW_FOR_SECONDS_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} slows the other character "
            f"of the event {event or '(none)'!r}. Only "
            f"{', '.join(SLOW_FOR_SECONDS_EVENTS)} names the enemy the trap "
            f"sentences slow.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} slows and states "
                f"{column} {written!r}. Its value is the slow in per cent and "
                f"nothing else, so the column must be empty.")


#: The four stats of traps layer 4, ruled 2026-10-21 by the coordinating
#: session (F1 to F9), each asked off the player with `Type.Trap` alone by a
#: reader of `ACataclysmMinion`, whose names `TrapRearmChanceStat`,
#: `TrapDamageToTriggeringEnemyStat`, `TrapTriggeringEnemyMaxHealthDamageStat`
#: and `TrapDamageOverTimeShareStat` hold:
#:
#: - "Triggered traps have a 25%-50% chance to reset and rearm instead of being
#:   consumed": a FLAT chance in per cent, rolled at every trigger (F1, F2);
#: - "Traps deal 100%-200% increased damage to the first enemy that triggers
#:   them": an INCREASE on the blows of a trigger's cast on the enemy that set it
#:   off, joined to the trap rows' increases (F4);
#: - "Traps deal bonus damage equal to 5%-10% of the triggering enemy's maximum
#:   HP": a FLAT share in per cent of that enemy's maximum health, added before
#:   the increases once per trigger (F5, F6, F7);
#: - "Traps deal 30%-50% of their damage as a DoT over 3 seconds to triggered
#:   enemies": a FLAT share in per cent of what a blow on that enemy took from
#:   health, laid on it over the sentence's 3 seconds, under the owner's rule of
#:   a tenth (F8, F9).
TRAP_REARM_CHANCE_STAT = "trap_rearm_chance"
TRAP_DAMAGE_TO_TRIGGERING_ENEMY_STAT = "trap_damage_to_triggering_enemy"
TRAP_TRIGGERING_ENEMY_MAX_HEALTH_DAMAGE_STAT = \
    "trap_triggering_enemy_max_health_damage"
TRAP_DAMAGE_OVER_TIME_SHARE_STAT = "trap_damage_over_time_share"

#: The one value kind each is read as. Any other works on a base of nought, or
#: is an increase of a figure nothing supplies, and changes NOTHING.
TRAP_BLOW_STAT_KINDS = {
    TRAP_REARM_CHANCE_STAT: "flat",
    TRAP_DAMAGE_TO_TRIGGERING_ENEMY_STAT: "increased",
    TRAP_TRIGGERING_ENEMY_MAX_HEALTH_DAMAGE_STAT: "flat",
    TRAP_DAMAGE_OVER_TIME_SHARE_STAT: "flat",
}

#: The Required Tags a row on any of them may state: none, or the trap's own
#: tag, the one tag all four are asked with.
TRAP_BLOW_STAT_REQUIRED_TAGS = frozenset({"", "Type.Trap"})

#: The most each may state, in per cent. A chance is at most 100; the three
#: others are bounds for typing mistakes, as `MAX_STATUS_SIZE` is: the
#: sentences say 200, 10 and 50.
TRAP_BLOW_STAT_MOST = {
    TRAP_REARM_CHANCE_STAT: 100.0,
    TRAP_DAMAGE_TO_TRIGGERING_ENEMY_STAT: 1000.0,
    TRAP_TRIGGERING_ENEMY_MAX_HEALTH_DAMAGE_STAT: 100.0,
    TRAP_DAMAGE_OVER_TIME_SHARE_STAT: 100.0,
}


def refuse_a_trap_blow_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on one of `TRAP_BLOW_STAT_KINDS` is one its reader adds.

    EACH IS READ AS ONE KIND, so a row of any other kind changes NOTHING; a tag
    but none or `Type.Trap` matches no trap, since all four are asked with
    `Type.Trap` alone; an action is not a stat line; and each figure is above
    nought and at most its bound, a chance at most 100. A CONDITION OR A SCALE
    IS REFUSED ELSEWHERE, as on every stat not on those checks' lists. A
    PASSIVE NODE'S ROW IS LEFT ALONE.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in TRAP_BLOW_STAT_KINDS or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != TRAP_BLOW_STAT_KINDS[stat]:
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads this stat as '{TRAP_BLOW_STAT_KINDS[stat]}' and "
                f"nothing else, so any other kind changes NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags not in TRAP_BLOW_STAT_REQUIRED_TAGS:
            problems.append(
                f"{who}: states {stat!r} under the required tags {tags!r}. "
                f"The game asks with 'Type.Trap' alone, so the row requires "
                f"nothing or 'Type.Trap'; any other tag matches no trap.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. This is "
                f"a stat line the game asks for, and an action row is not "
                f"one.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if not 0.0 < value <= TRAP_BLOW_STAT_MOST[stat]:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, which is above 0 "
                    f"and at most {TRAP_BLOW_STAT_MOST[stat]:g}.")
    return problems


#: The action that makes an enemy the wearer kills EXPLODE FOR A SHARE OF THE
#: BLOW THAT KILLED IT, with its value as the share of that blow's figure
#: before the victim's mitigation, in per cent, dealt to every enemy within 4
#: metres of the body. Ruled 2026-10-21 by the coordinating session (traps
#: layer 4, F10 to F12): "Enemies killed by traps explode, dealing 50%-100% of
#: the trap's damage to all enemies within 4 meters", whose row requires
#: `Type.Trap`, which the kill carries as the killing skill's tags for a blow
#: of a trap's cast. `UCataclysmAbilitySystemComponent::
#: ExplodeVictimForKillingBlowAction` holds the same name.
#:
#: ON `kill` AND NO OTHER EVENT, as `OVERKILL_EXPLOSION_ACTION`: the event loop
#: only records the share, and the kill site takes it once the loop has
#: returned and makes the explosion, for a kill by a hit and never by a tick.
KILLING_BLOW_EXPLOSION_ACTION = "explode_victim_for_killing_blow"
KILLING_BLOW_EXPLOSION_EVENTS = ("kill",)

#: An enemy the wearer kills BURSTS FOR A SHARE OF ITS MAXIMUM HEALTH to the
#: enemies near the body: the Soul Collector's The Ferryman, the Death trees'
#: slice C4c1 (2026-11-25). The value is the share, in per cent.
#: `UCataclysmAbilitySystemComponent::BurstSlainForMaximumHealthAction` holds
#: the same name. ON `kill` AND NO OTHER EVENT, as the two explosions: the loop
#: records the share and the kill handler makes the burst.
SLAIN_BURST_ACTION = "burst_slain_for_share_of_maximum_health"
SLAIN_BURST_EVENTS = ("kill",)

#: The wearer's Bleed left on an enemy that died, passed to the strongest
#: enemy within 10 metres of the body over the value's seconds: the Shadow's
#: Blood Debt, the Death trees' slice C4c2 (2026-11-25).
#: `UCataclysmAbilitySystemComponent::PassBleedLeftToStrongestAction` holds the
#: same name. ON `afflicted_death` AND NO OTHER EVENT: the one event that names
#: a body carrying the wearer's ailment, raised with that ailment's tags.
BLEED_LEFT_TO_STRONGEST_ACTION = "pass_bleed_left_to_strongest"
BLEED_LEFT_TO_STRONGEST_EVENTS = ("afflicted_death",)

#: The wearer's next hit that lands within the value's seconds is a critical
#: strike: the Shadow's Chain Execution, the Death trees' slice C4d1
#: (2026-11-25). `UCataclysmAbilitySystemComponent::CriticalNextHitAction`
#: holds the same name. ON `kill` AND NO OTHER EVENT.
NEXT_HIT_CRITICAL_ACTION = "next_hit_critical"
#: ON A KILL, AN EVASION OR A FULL BAR: Chain Execution's `kill`, and since the
#: Death trees' slice C6a (2026-11-25) Slip's `dodge` ("After you evade a hit")
#: and Brim's `resource_full` ("Each time your Fervour reaches its maximum").
NEXT_HIT_CRITICAL_EVENTS = ("kill", "dodge", "resource_full")

#: The most seconds the charge may last. A bound for typing mistakes; the
#: sentences are 2 and 3. NOUGHT IS "UNTIL USED", for Brim's "your next hit",
#: which states no time (slice C6a, a judgement).
MAX_NEXT_HIT_CRITICAL_SECONDS = 10.0

#: A running hold before the Fervour decay lengthened by the value's seconds,
#: never past its full length: the Shadow's Keep the Count, the Death trees'
#: slice C6a (2026-11-25). `UCataclysmAbilitySystemComponent::FervourHoldExtendedAction`
#: holds the same name. ON `dodge` AND NO OTHER EVENT.
FERVOUR_HOLD_EXTENDED_ACTION = "fervour_hold_extended"
FERVOUR_HOLD_EXTENDED_EVENTS = ("dodge",)

#: The most seconds one row may add. A bound for typing mistakes; the one
#: sentence is 1.
MAX_FERVOUR_HOLD_EXTENDED_SECONDS = 10.0

#: The critical strike charge an evasion grants holds two rather than one, on a
#: roll of the row's Chance Percent: the Shadow's Twice Answered, the Death
#: trees' slice C6b4 (2026-11-25), as ruled. Its value is the count, 2.
#: `UCataclysmAbilitySystemComponent::CriticalTwiceOnEvasionAction` holds the
#: same name. ON `dodge` AND NO OTHER EVENT: "each evasion".
#: Blade Echo and Thousand Cuts, the Shadow's 50 and 200 vows' first options,
#: the Death trees' slice C6e (2026-11-25), as ruled (Q6): on a kill, an instant
#: hit of the value's per cent of weapon damage on the wearer's own mark within
#: 10 metres, else the strongest enemy that near; Thousand Cuts moves the wearer
#: beside it first. On `kill` only. The value is the per cent, up to
#: `MAX_STRIKE_TARGET_PERCENT`.
STRIKE_STRONGEST_NEAR_ACTION = "strike_strongest_near"
DASH_TO_STRONGEST_AND_STRIKE_ACTION = "dash_to_strongest_and_strike"
STRONGEST_NEAR_ACTIONS = (STRIKE_STRONGEST_NEAR_ACTION, DASH_TO_STRONGEST_AND_STRIKE_ACTION)
STRONGEST_NEAR_EVENTS = ("kill",)

NEXT_HIT_CRITICAL_TWICE_ACTION = "next_hit_critical_twice"
NEXT_HIT_CRITICAL_TWICE_EVENTS = ("dodge",)
NEXT_HIT_CRITICAL_TWICE_COUNT = 2.0

#: The enemy modifiers Wearer of the Dead can lend, in their fixed places,
#: counted from 1: a `wearing_enemy_modifier` row's value names one. The Death
#: trees' slice C4d3 (2026-11-25), as ruled (W1): the five built modifiers that
#: are a plain number on a player stat. `UCataclysmFervour::WornEnemyModifierRows`
#: holds the same list, and a test holds the two equal. Appended to when a
#: modifier is built on enemies and is a plain number too, never reordered. TO
#: ADD ONE: append it here and to the engine's list, and give the vow its row at
#: the new place; the condition's upper bound follows from this list's length.
#: Issue #2899 builds every enemy modifier but the one button ability to work on
#: any wearer and adds each here; the first expected source is the branch
#: `feat/death-war-enemy-modifiers`.
WORN_ENEMY_MODIFIERS = (
    "Generic_Titanic_Resolve",
    "Generic_Overpowered",
    "Generic_Bloodthirsty",
    "Generic_Unyielding",
    "Generic_Relentless",
)
CONDITIONS["wearing_enemy_modifier"] = (
    1.0, float(len(WORN_ENEMY_MODIFIERS)), CONDITIONS["wearing_enemy_modifier"][2])

#: A cooldown reduction's Fraction Of that acts once for each whole hundred of
#: the class resource held when the row acts: Relentless, the Death trees'
#: slice C4d1 (2026-11-25). Accepted on the cooldown reductions alone.
FRACTION_OF_HUNDREDS_HELD = "for_each_hundred_class_resource_held"

#: The most seconds the Bleed left may be dealt over. A bound for typing
#: mistakes; the one sentence is 2.
MAX_BLEED_LEFT_SECONDS = 10.0

#: The most its share may be, in per cent of the slain creature's maximum
#: health. A bound for typing mistakes; the one sentence is 10.
MAX_SLAIN_BURST_SHARE = 100.0

#: The most its share may be, in per cent. A bound for typing mistakes, as
#: `MAX_OVERKILL_EXPLOSION_SHARE` is; the one sentence is 50 to 100.
MAX_KILLING_BLOW_EXPLOSION_SHARE = 1000.0


def _check_killing_blow_explosion_action(index: int, who: str, action: str,
                                         event: str, fraction_of: str,
                                         kind: str, raw,
                                         headers: dict[str, int]) -> None:
    """Everything a killing blow explosion row must say, and everything it
    must not. Ruled 2026-10-21 (traps layer 4, F10 to F12). ON `kill` ONLY,
    for the reason `_check_overkill_explosion_action` gives; the share is
    checked where the value is read. A fraction, a value kind and a scale each
    mean nothing here, so each is refused rather than dropped.
    """
    if event not in KILLING_BLOW_EXPLOSION_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} explodes a killed enemy "
            f"for a share of the blow that killed it on the event "
            f"{event or '(none)'!r}. Only a kill has a victim and a killing "
            f"blow: {', '.join(KILLING_BLOW_EXPLOSION_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} explodes a killed enemy "
                f"for a share of the blow that killed it and states {column} "
                f"{written!r}. Its value is that share and nothing else, so the "
                f"column must be empty.")


def _check_bleed_left_to_strongest_action(index: int, who: str, action: str,
                                          event: str, fraction_of: str,
                                          kind: str, raw,
                                          headers: dict[str, int]) -> None:
    """Everything a Blood Debt row must say, and everything it must not. The
    Death trees' slice C4c2 (2026-11-25). ON `afflicted_death` ONLY, the one
    event naming a body that carried the wearer's ailment; the seconds are
    checked where the value is read. A fraction, a value kind and a scale each
    mean nothing here, so each is refused rather than dropped.
    """
    if event not in BLEED_LEFT_TO_STRONGEST_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} passes a dead enemy's "
            f"Bleed left on the event {event or '(none)'!r}. Only the death of "
            f"an enemy carrying the wearer's ailment names that body: "
            f"{', '.join(BLEED_LEFT_TO_STRONGEST_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} passes a dead enemy's "
                f"Bleed left and states {column} {written!r}. Its value is the "
                f"seconds and nothing else, so the column must be empty.")


def _check_next_hit_critical_action(index: int, who: str, action: str,
                                    event: str, fraction_of: str, kind: str,
                                    raw, headers: dict[str, int]) -> None:
    """Everything a Chain Execution row must say, and everything it must not.
    The Death trees' slice C4d1 (2026-11-25). ON `kill` ONLY; the seconds are
    checked where the value is read. A fraction, a value kind and a scale each
    mean nothing here, so each is refused rather than dropped.
    """
    if event not in NEXT_HIT_CRITICAL_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} makes the next hit a "
            f"critical strike on the event {event or '(none)'!r}. Only a kill, "
            f"an evasion or a full bar grants it: "
            f"{', '.join(NEXT_HIT_CRITICAL_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} makes the next hit a "
                f"critical strike and states {column} {written!r}. Its value is "
                f"the seconds and nothing else, so the column must be empty.")


def _check_fervour_hold_extended_action(index: int, who: str, action: str,
                                        event: str, fraction_of: str, kind: str,
                                        raw, headers: dict[str, int]) -> None:
    """Everything a Keep the Count row must say, and everything it must not.
    The Death trees' slice C6a (2026-11-25). ON `dodge` ONLY; the seconds are
    checked where the value is read. A fraction, a value kind and a scale each
    mean nothing here, so each is refused rather than dropped.
    """
    if event not in FERVOUR_HOLD_EXTENDED_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} lengthens the hold before "
            f"decay on the event {event or '(none)'!r}. Only an evasion does: "
            f"{', '.join(FERVOUR_HOLD_EXTENDED_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} lengthens the hold "
                f"before decay and states {column} {written!r}. Its value is "
                f"the seconds and nothing else, so the column must be empty.")


def _check_strongest_near_action(index: int, who: str, action: str,
                                 event: str, fraction_of: str, kind: str,
                                 raw, headers: dict[str, int]) -> None:
    """Everything a Blade Echo or Thousand Cuts row must say, and everything it
    must not. The Death trees' slice C6e (2026-11-25). ON `kill` ONLY; the per
    cent is checked where the value is read. A fraction, a value kind and a scale
    each mean nothing here, so each is refused rather than dropped.
    """
    if event not in STRONGEST_NEAR_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} strikes the strongest enemy "
            f"near on the event {event or '(none)'!r}. Only a kill does: "
            f"{', '.join(STRONGEST_NEAR_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} strikes the strongest "
                f"enemy near and states {column} {written!r}. Its value is the "
                f"per cent and nothing else, so the column must be empty.")


def _check_next_hit_critical_twice_action(index: int, who: str, action: str,
                                          event: str, fraction_of: str, kind: str,
                                          raw, headers: dict[str, int]) -> None:
    """Everything a Twice Answered row must say, and everything it must not.
    The Death trees' slice C6b4 (2026-11-25). ON `dodge` ONLY; the count is
    checked where the value is read, and the chance is the Chance Percent. A
    fraction, a value kind and a scale each mean nothing here, so each is
    refused rather than dropped.
    """
    if event not in NEXT_HIT_CRITICAL_TWICE_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} doubles a critical strike "
            f"charge on the event {event or '(none)'!r}. Only an evasion does: "
            f"{', '.join(NEXT_HIT_CRITICAL_TWICE_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} doubles a critical "
                f"strike charge and states {column} {written!r}. Its value is "
                f"the count and nothing else, so the column must be empty.")


def _check_slain_burst_action(index: int, who: str, action: str, event: str,
                              fraction_of: str, kind: str, raw,
                              headers: dict[str, int]) -> None:
    """Everything a slain creature's burst row must say, and everything it
    must not. The Death trees' slice C4c1 (2026-11-25). ON `kill` ONLY, for the
    reason `_check_overkill_explosion_action` gives; the share is checked where
    the value is read. A fraction, a value kind and a scale each mean nothing
    here, so each is refused rather than dropped.
    """
    if event not in SLAIN_BURST_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} bursts a killed enemy for "
            f"a share of its maximum health on the event {event or '(none)'!r}. "
            f"Only a kill has a slain creature: {', '.join(SLAIN_BURST_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} bursts a killed enemy "
                f"for a share of its maximum health and states {column} "
                f"{written!r}. Its value is that share and nothing else, so the "
                f"column must be empty.")


#: The two actions of traps layer 5, ruled 2026-10-22 by the coordinating
#: session (H1 to H11) and, on H5, by the owner: what a row does to the
#: wearer's OTHER skill traps. `ARM_TRAPS_NEARBY_ACTION` ARMS each standing,
#: unarmed trap of the wearer's within the row's metres, as if its arming time
#: had passed, and sets off none: "When a trap is triggered, the nearest unarmed
#: trap within 10 meters arms instantly", "When a trap triggers it releases a
#: shockwave that arms all traps within 15 meters instantly" and "When a gadget
#: is destroyed it arms all traps within 12 meters instantly".
#: `TRIGGER_TRAPS_NEARBY_ACTION` SETS EACH OFF as its own trigger does, each at
#: its own triggering enemy: "Traps trigger simultaneously when any one of them
#: is triggered, if within 8-12 meters of each other", "When a gadget is
#: destroyed, the nearest active trap within 10 meters is triggered
#: immediately" and the trap half of "When you die, all your active traps and
#: gadgets immediately trigger simultaneously". A trap a row sets off counts as
#: triggered, and its own `trap_triggered` is answered after the row (the
#: owner's H5 (b)), each trap once a chain.
#: `UCataclysmAbilitySystemComponent::ArmTrapsNearbyAction` and
#: `TriggerTrapsNearbyAction` hold the same names.
#:
#: THE VALUE IS THE RADIUS IN METRES (H10), measured from the trap that
#: triggered or the gadget destroyed. ON `player_death` IT IS NOUGHT, every
#: trap: that event names no place. SCALE MAX STEPS IS THE COUNT (H11): 1 for
#: "the nearest" trap alone, empty for every trap; no other count is stated by
#: any sentence. NO TRIGGER COOLDOWN (H9), which `takes_a_trigger_cooldown`
#: refuses for them.
ARM_TRAPS_NEARBY_ACTION = "arm_traps_nearby"
TRIGGER_TRAPS_NEARBY_ACTION = "trigger_traps_nearby"
TRAPS_NEARBY_ACTIONS = (ARM_TRAPS_NEARBY_ACTION, TRIGGER_TRAPS_NEARBY_ACTION)

#: The events each may hang on: the ones its sentences name. A trap's
#: triggering and a gadget's destruction carry the place measured from; the
#: wearer's death sets off every trap, and arms none.
TRAPS_NEARBY_EVENTS = {
    ARM_TRAPS_NEARBY_ACTION: ("trap_triggered", "gadget_destroyed"),
    TRIGGER_TRAPS_NEARBY_ACTION: ("trap_triggered", "gadget_destroyed",
                                  "player_death"),
}

#: The event on which the radius is nought, every trap, and the count every
#: trap.
TRAPS_NEARBY_EVERY_TRAP_EVENTS = ("player_death",)

#: The largest radius a row may state, in metres. A bound for typing mistakes,
#: as `MAX_TELEPORT_RANDOMLY_METRES` is; the sentences say 8 to 15.
MAX_TRAPS_NEARBY_METRES = 50.0


def _check_traps_nearby_action(index: int, who: str, action: str, event: str,
                               fraction_of: str, kind: str, raw,
                               headers: dict[str, int]) -> None:
    """Everything a row on the wearer's other traps must say, and everything
    it must not. Ruled 2026-10-22 (traps layer 5, H1 to H11). The event must be
    one its sentences name; the radius and the count are checked where the
    value and Scale Max Steps are read. A fraction, a value kind and a scale
    each mean nothing here, so each is refused rather than dropped, and
    `player_death` carries no tags for a scoped row to match.
    """
    if event not in TRAPS_NEARBY_EVENTS[action]:
        raise DataError(
            f"Enchantment Effects row {index}: {who} acts on the wearer's "
            f"other traps with {action} on the event {event or '(none)'!r}. "
            f"The game does it on {', '.join(TRAPS_NEARBY_EVENTS[action])} "
            f"and on nothing else.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} acts on the wearer's "
                f"other traps and states {column} {written!r}. Its value is a "
                f"radius in metres and nothing else, so the column must be "
                f"empty.")
    tags = clean(_cell(raw, headers, "Required Tags"))
    if event in TRAPS_NEARBY_EVERY_TRAP_EVENTS and tags:
        raise DataError(
            f"Enchantment Effects row {index}: {who} acts on the wearer's "
            f"traps on {event!r} and states Required Tags {tags!r}. That event "
            f"carries no tags, so the row would never fire.")


#: The action of traps layer 6, ruled 2026-10-22 by the coordinating session
#: (J1 to J10, J2 changed: skill gadgets only): THE WEARER'S NEAREST STANDING
#: SKILL GADGET FIRES ONCE AT THE ENEMY THAT SET A TRAP OFF, casting its skill
#: once, free, at that enemy, priced and announced as its clock's cast, its
#: clock unchanged: "When a trap is triggered near an active gadget, the gadget
#: fires an immediate bonus shot at the triggering enemy" and "For every 3 traps
#: triggered, your nearest gadget fires a massive overcharged shot dealing
#: 200%-400% of its normal damage". War's gadgets do not fire, by the no-War
#: mandate. `UCataclysmAbilitySystemComponent::GadgetFiresAtTrapEnemyAction`
#: holds the same name.
#:
#: THE VALUE IS A "MORE" IN PER CENT ON THAT ONE SHOT (J6): 100 the gadget's
#: normal blow, 200 to 400 the overcharged shot. EVENT VALUE IS THE METRES FROM
#: THE TRAP the gadget must stand within, 10 for "near" (J1), empty or nought for
#: any distance (J5). EVERY NTH, when stated, is the count of the wearer's
#: triggered traps it fires on, with no window (J4). NO TRIGGER COOLDOWN (J9),
#: which `takes_a_trigger_cooldown` refuses for it.
GADGET_FIRES_AT_TRAP_ENEMY_ACTION = "gadget_fires_at_trap_enemy"

#: The event it may hang on: a trap's triggering, which names the trap and the
#: enemy that set it off.
GADGET_FIRES_AT_TRAP_ENEMY_EVENTS = ("trap_triggered",)

#: The most its "more" may be, in per cent. A bound for typing mistakes, as
#: `MAX_KILLING_BLOW_EXPLOSION_SHARE` is; the sentences say 100 and 200 to 400.
MAX_GADGET_SHOT_AT_TRAP_ENEMY_PERCENT = 1000.0

#: The largest radius a row may state, in metres, as `MAX_TRAPS_NEARBY_METRES`.
MAX_GADGET_SHOT_AT_TRAP_ENEMY_METRES = 50.0


def _check_gadget_fires_at_trap_enemy_action(index: int, who: str, action: str,
                                             event: str, fraction_of: str,
                                             kind: str, raw,
                                             headers: dict[str, int]) -> None:
    """Everything a row firing the wearer's gadget at a trap's enemy must say,
    and everything it must not. Ruled 2026-10-22 (traps layer 6, J1 to J10). ON
    `trap_triggered` ONLY; the "more", the radius and the count are checked
    where the value, Event Value and Every Nth are read. A fraction, a value
    kind and a scale each mean nothing here, so each is refused rather than
    dropped."""
    if event not in GADGET_FIRES_AT_TRAP_ENEMY_EVENTS:
        raise DataError(
            f"Enchantment Effects row {index}: {who} fires the wearer's gadget "
            f"at a trap's enemy on the event {event or '(none)'!r}. Only a "
            f"trap's triggering names the trap and its enemy: "
            f"{', '.join(GADGET_FIRES_AT_TRAP_ENEMY_EVENTS)}.")
    for column, written in (("Fraction Of", fraction_of),
                            ("Value Kind", kind),
                            ("Scale", clean(_cell(raw, headers, "Scale")))):
        if written:
            raise DataError(
                f"Enchantment Effects row {index}: {who} fires the wearer's "
                f"gadget at a trap's enemy and states {column} {written!r}. Its "
                f"value is the shot's \"more\" in per cent and nothing else, so "
                f"the column must be empty.")


#: The stat of traps layer 6, ruled 2026-10-22 by the coordinating session (J7,
#: J8), asked off the player with `Type.Trap` alone by
#: `ACataclysmMinion::SkillTrapPullRadiusCmFor`, whose name
#: `TrapPullRadiusStat` holds: "When a trap triggers, all enemies within 4-6
#: meters are pulled toward the blast center", A FLAT RADIUS IN METRES, every
#: living enemy within which a trap pulls to 1 m of itself before its cast.
TRAP_PULL_RADIUS_STAT = "trap_pull_radius"

#: The largest radius it may state, in metres; the sentence says 4 to 6.
MAX_TRAP_PULL_RADIUS_METRES = 50.0


def refuse_a_trap_pull_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `TRAP_PULL_RADIUS_STAT` is one its reader adds.

    IT IS READ AS FLAT, so a row of any other kind changes NOTHING; a tag but
    none or `Type.Trap` matches no trap, since it is asked with `Type.Trap`
    alone; an action is not a stat line; and the radius is above nought and at
    most the bound. A CONDITION OR A SCALE IS REFUSED ELSEWHERE, as on every
    stat not on those checks' lists. A PASSIVE NODE'S ROW IS LEFT ALONE.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat != TRAP_PULL_RADIUS_STAT or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"game reads this stat as 'flat' and nothing else, so any other "
                f"kind changes NOTHING.")
        tags = str(row.get("RequiredTags") or "").strip()
        if tags not in TRAP_BLOW_STAT_REQUIRED_TAGS:
            problems.append(
                f"{who}: states {stat!r} under the required tags {tags!r}. "
                f"The game asks with 'Type.Trap' alone, so the row requires "
                f"nothing or 'Type.Trap'; any other tag matches no trap.")
        action = str(row.get("Action") or "").strip()
        if action:
            problems.append(
                f"{who}: states {stat!r} with the action {action!r}. This is "
                f"a stat line the game asks for, and an action row is not "
                f"one.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if not 0.0 < value <= MAX_TRAP_PULL_RADIUS_METRES:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}, which is above 0 "
                    f"and at most {MAX_TRAP_PULL_RADIUS_METRES:g}.")
    return problems

#: "This weapon has 2-4 damage types", ruled 2026-10-25 by the coordinating
#: session (Q-D2 to Q-D6): an effect row with an item-level stat,
#: `damage_types_carried`, flat 2 to 4, READ BY THE DROP and not by any
#: character. `UCataclysmDropRoll::DamageTypesCarriedBy` finds the item's drawn
#: enchantments' rows on it, works out the figure at the item's own roll and
#: rounds it; `TopUpDamageTypes` then adds damage types until the weapon holds
#: that many. `UCataclysmDropRoll::DamageTypesCarriedStat` holds the name.
DAMAGE_TYPES_CARRIED_STAT = "damage_types_carried"

#: The most a row may state: eight, the most damage types any weapon base holds
#: and the number there are. The one sentence says 2 to 4.
MAX_DAMAGE_TYPES_CARRIED = 8


def refuse_a_damage_types_carried_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `DAMAGE_TYPES_CARRIED_STAT` is one the drop reads.

    THE DROP READS ITS TWO ENDS AND NOTHING ELSE: the figure at the item's roll,
    rounded to a whole number of damage types. So the row is flat; it requires
    no tag, since the drop matches none; it has no action, condition or scale,
    none of which the drop reads; and each end is a whole number from 1 to
    `MAX_DAMAGE_TYPES_CARRIED`. A PASSIVE NODE'S ROW IS LEFT ALONE.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat != DAMAGE_TYPES_CARRIED_STAT or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        if kind != "flat":
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The "
                f"drop reads the row's figure itself, so the row must be "
                f"'flat'.")
        for column, said in (("RequiredTags", "required tags"), ("Action", "action"),
                             ("Condition", "condition"), ("Condition2", "second condition"),
                             ("Scale", "scale")):
            written = str(row.get(column) or "").strip()
            if written:
                problems.append(
                    f"{who}: states {stat!r} with the {said} {written!r}. The "
                    f"drop reads the row's two ends and nothing else, so the "
                    f"cell must be empty.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if not (value == int(value) and 1 <= value <= MAX_DAMAGE_TYPES_CARRIED):
                problems.append(
                    f"{who}: states {value:g} on {stat!r}. A count of damage "
                    f"types is a whole number from 1 to "
                    f"{MAX_DAMAGE_TYPES_CARRIED}.")
    return problems


#: "Sockets on this equipment fit any gem type" and "Gems in this equipment are
#: 50% less effective", issue #46 slice G10, ruled by the coordinating session on
#: 2026-11-22: effect rows with item-level stats, READ OFF THE PIECE'S OWN DRAWN
#: ENCHANTMENTS and by no character, as `damage_types_carried` is.
#: `UCataclysmGems::FitsAnyGem` asks whether a row on the first states more than
#: nought; `UCataclysmGems::SocketedGemEffectOf` multiplies each of the piece's
#: gems by one plus each row's value on the second over a hundred. The engine
#: holds the names in `UCataclysmGems::SocketFitsAnyGemStat` and
#: `SocketedGemEffectStat`.
SOCKET_FITS_ANY_GEM_STAT = "socket_fits_any_gem"
SOCKETED_GEM_EFFECT_STAT = "socketed_gem_effect"


def refuse_a_gem_enchantment_row_the_game_cannot_read(
        sheet: str, rows: list[dict]) -> list[str]:
    """A row on `SOCKET_FITS_ANY_GEM_STAT` or `SOCKETED_GEM_EFFECT_STAT` is one
    the gem code reads.

    THE GEM CODE READS EACH ROW'S TWO ENDS AT THE PIECE'S ROLL AND NOTHING ELSE.
    So neither row requires a tag, since nothing matches one; neither has an
    action, condition or scale, none of which is read; and:

      fits any gem      flat, both ends 1: a flag, asked whether above nought
      gem effect        `more`, the bucket its "less" names, each end above -100
                        and not nought, since -100 would leave the gems worth
                        nothing and nought would change nothing

    A PASSIVE NODE'S ROW IS LEFT ALONE: nothing reads these stats off a node.
    """
    problems = []
    for row in rows:
        stat = str(row.get("Stat") or "").strip()
        if stat not in (SOCKET_FITS_ANY_GEM_STAT, SOCKETED_GEM_EFFECT_STAT) or "Node" in row:
            continue
        who = f"{sheet}/{row['Name']}"
        kind = str(row.get("ValueKind") or "").strip().lower()
        wanted = "flat" if stat == SOCKET_FITS_ANY_GEM_STAT else "more"
        if kind != wanted:
            problems.append(
                f"{who}: states {stat!r} with the value kind {kind!r}. The gem "
                f"code reads the row's figure itself, so the row must be "
                f"{wanted!r}.")
        for column, said in (("RequiredTags", "required tags"), ("Action", "action"),
                             ("Condition", "condition"), ("Condition2", "second condition"),
                             ("Scale", "scale")):
            written = str(row.get(column) or "").strip()
            if written:
                problems.append(
                    f"{who}: states {stat!r} with the {said} {written!r}. The "
                    f"gem code reads the row's two ends and nothing else, so the "
                    f"cell must be empty.")
        for column in ("ValueLow", "ValueHigh"):
            value = float(row.get(column) or 0.0)
            if stat == SOCKET_FITS_ANY_GEM_STAT and value != 1.0:
                problems.append(
                    f"{who}: states {value:g} on {stat!r}. Whether a piece's "
                    f"sockets fit any gem is a flag, stated as 1.")
            if stat == SOCKETED_GEM_EFFECT_STAT and not (-100.0 < value and value != 0.0):
                problems.append(
                    f"{who}: states {value:g} on {stat!r}. A change to how "
                    f"effective a piece's gems are is above -100 and not "
                    f"nought.")
    return problems


#: The scales that count bodies standing inside a radius.
#:
#: EACH NEEDS THE ROW TO STATE THAT RADIUS, in a `Reach Metres` column, because
#: the radius belongs to the row and not to the character:
#: `UCataclysmAbilitySystemComponent::WithEnemiesInReach` walks the level only
#: when some modifier states a reach above zero, and counts each row's entries
#: inside the row's own reach.
SCALES_THAT_COUNT_ENEMIES_IN_A_RADIUS = frozenset({
    "enemies_in_reach",
    "crippled_enemies_in_reach",
    # AND THE SUMMONER'S OTHER MINIONS NEAR THE MINION ASKING, the finishing of
    # the Death Cataclysm, slice C (2026-11-26): bodies inside the row's radius,
    # counted from a minion rather than from the character, and needing the
    # radius for the same reason.
    "minions_near_the_minion",
})


def refuse_a_reach_scale_a_row_cannot_state(sheet: str,
                                            rows: list[dict]) -> list[str]:
    """A row may only count enemies in a radius where it can state the radius.

    THE TWO EFFECT SHEETS SHARE THE SCALE VOCABULARY AND NOT THE COLUMNS.
    `_condition_and_scale` validates `Scale` for both, and accepts both names
    above on either. Only the Passive Effects sheet has a `Reach Metres` column,
    and only `validate_passive_effects` checks it.

    SO SUCH A ROW ON THE OTHER SHEET IS ACCEPTED AND DEAD. Its modifier keeps
    `ReachMetres` at -1, the level walk never happens, the distance list stays
    empty, and the row multiplies by a count of nobody. Nothing errors and
    nothing warns -- the same silent shape issue #1973 was filed for, one level
    down: that check asks whether anything asks for the row's STAT, and a row
    like this names a stat that is asked for. Issue #1987.

    SCOPED BY WHETHER THE ROW CAN CARRY A REACH AT ALL, rather than by naming
    the sheet here. A row built with a `ReachMetres` key is on a sheet with the
    column and is `validate_passive_effects`'s business; a row without one could
    not state a reach whatever its author intended. That also means this check
    stops refusing by itself on the day the other sheet gains the column -- at
    which point it needs the value rule the passive sheet already has, which
    this does not supply.
    """
    problems = []
    for row in rows:
        if "ReachMetres" in row:
            continue
        scale = str(row.get("Scale") or "").strip().lower()
        if scale in SCALES_THAT_COUNT_ENEMIES_IN_A_RADIUS:
            problems.append(
                f"{sheet}/{row['Name']}: scales by {scale!r}, which counts the "
                f"enemies standing within a radius, and no row on this sheet "
                f"can state one -- it has no 'Reach Metres' column. The "
                f"modifier would keep a reach of -1, the engine would never "
                f"walk the level for it, and the row would count nobody and "
                f"grant NOTHING without saying so. Issue #1987. Use a scale "
                f"this sheet can carry, or give the sheet the column and the "
                f"rule that goes with it.")
    return problems


def validate_passive_effects(tables: dict[str, list[dict]],
                             known: set[str]) -> list[str]:
    """Every passive effect names a real node, a real stat and declared tags.

    ALL THREE FAIL SILENTLY WITHOUT THIS, and none of them errors at run time:

      a node key that does not exist   the effect is never applied to anything
      a stat nothing else supplies     the increase multiplies a base of zero
      an undeclared tag                the modifier's required tag matches
                                       nothing, so it applies to nothing

    A `flat` ROW IN THIS SHEET SUPPLIES A STAT TOO, and that is why the middle
    check is not simply "some class line names it". The complaint it exists for
    is an INCREASE with no base to multiply. A flat row IS the base: the
    Masochist's starting node grants 1 Fervour gained per 1% of maximum health
    lost, and the three rates it supplies are zero for every class on purpose,
    so no class stat line names them and none should -- a class stat row zero in
    both columns says nothing, and `test_class_sheets_match_the_model.py` holds
    that rule. Issue #954.

    A `flat` IMPLICIT ON AN ITEM BASE SUPPLIES ONE TOO, for the same reason and
    with the same argument. Issue #958. `attack_damage` is the case: no class
    line names it and none should, because a character's damage comes from the
    weapon in its hands. `UCataclysmPlayerClassStats` says so in as many words --
    "A weapon's damage is an `attack_damage` implicit on its base, so it already
    arrives as a flat modifier and the base is correctly zero" -- and a node
    reading "+2% increased damage per point" has a real base under it the moment
    a weapon is held.

    AND SO DOES AN ITEM BASE COLUMN, which is a second route from a weapon and
    not the same one. Issue #1002. A swing rate is a COLUMN on the base rather
    than an implicit, because two weapons average theirs and an implicit cannot
    be averaged, and `UCataclysmPlayerClassStats::StatBasesFromWeapons` turns it
    into the base. `attack_speed` is the case: no class line names it and none
    should, and an `increased` row on it has a real base under it the moment a
    weapon is held. See `item_base_column_stats` above.

    A ROLLED AFFIX IS DELIBERATELY NOT COUNTED, and the difference is not
    pedantic. An implicit is on EVERY item of that base type, so a Sword always
    carries attack damage; an affix may never roll at all. A stat whose only
    supplier is an optional affix really is zero for most characters, and an
    increase on it really is worth nothing, which is exactly what this check is
    for. Counting affixes would admit twelve more stats and blunt it.

    WHAT STILL CATCHES A TYPO IN A STAT NAME IS THE ENGINE SIDE.
    `Cataclysm.Passives.EveryStatAPassiveNodeGrantsHasAnAttributeBehindIt` reads
    this file and fails when a name has no gameplay attribute behind it, which is
    the failure that really matters: a stat with no attribute is dropped by
    `UCataclysmPlayerClassStats::ApplyTo` in silence.
    """
    effects = tables.get("PassiveEffects")
    nodes = tables.get("PassiveNodes")
    class_rows = tables.get("ClassStats")
    attribute_rows = tables.get("Attributes")
    if not effects or not nodes:
        return []

    node_names = {row["Name"] for row in nodes}

    # HOW MANY POINTS EACH NODE HOLDS, for the threshold check below. Issue
    # #1755.
    max_points = {row["Name"]: int(row.get("MaxPoints", 0) or 0) for row in nodes}

    # WHICH NODES ARE CAPSTONES, for the option check below. Issue #1029. The
    # `Kind` column is the same one `UCataclysmPassiveTree::CapstoneKind` matches
    # against in the engine.
    capstones = {row["Name"] for row in nodes
                 if str(row.get("Kind", "")).strip().lower() == "capstone"}
    # AND WHAT EACH CAPSTONE ITSELF SAYS, for the one row a capstone may carry
    # with no option: a clause its own sentence states (`capstone_states_a_clause`).
    capstone_sentences = {row["Name"]: str(row.get("Description", "")) for row in nodes
                          if row["Name"] in capstones}

    stats = ({row["Stat"] for row in class_rows} if class_rows else set()) | \
            ({row["Stat"] for row in attribute_rows} if attribute_rows else set()) | \
            {row["Stat"] for row in effects
             if str(row["ValueKind"]).lower() == "flat"} | \
            item_base_flat_stats(tables.get("ItemBases")) | \
            item_base_column_stats(tables.get("ItemBases")) | \
            set(ENGINE_SUPPLIED_BASES) | \
            stats_with_no_attribute()

    problems = []
    for row in effects:
        # THE `Node` COLUMN AND NOT THE ROW NAME. They were the same string
        # until issue #953; the row name now carries a `#1` suffix so that one
        # node can have several rows, and checking it against the node table
        # would report every row as missing.
        if row["Node"] not in node_names:
            problems.append(
                f"PassiveEffects/{row['Name']}: no passive node is called "
                f"{row['Node']}, so the effect reaches nothing")

        # AN ACTION ROW HAS NO STAT, since the passive row port of 2026-11-24,
        # so the two stat checks below are not asked of it, as
        # `validate_enchantment_effects` does not ask them of an action row.
        is_action = bool(row.get("Action"))

        # A REMOVAL IS NOT ASKED FOR A BASE, for the reason
        # `validate_enchantment_effects` gives: it multiplies nothing. Issue #1791.
        # The two sheets share one vocabulary of kinds, so they share this too.
        if (stats and not is_action and str(row["ValueKind"]).lower() != "removed"
                and row["Stat"] not in stats):
            problems.append(
                f"PassiveEffects/{row['Name']}: {row['Stat']!r} is not a stat "
                f"any class line or attribute names, and no flat row in this "
                f"sheet supplies it either")

        # AND A FLAT ROW ON A STAT THE GAME READS NOWHERE. Issue #2582, the
        # passive half of #2387. A flat row supplies its own stat to the base
        # check above, so it was never asked whether the game reads that stat.
        # A node's rows reach the character through the same
        # `UCataclysmPlayerClassStats::ApplyTo` that records a stat only when it
        # is in `StatToAttribute` or `StatsWithNoAttribute`.
        if (not is_action and str(row["ValueKind"]).lower() == "flat"
                and row["Stat"] not in stats_with_an_attribute()
                and row["Stat"] not in stats_with_no_attribute()):
            problems.append(
                f"PassiveEffects/{row['Name']}: {row['Stat']!r} is a flat row "
                f"on a stat the game reads nowhere: it is in neither "
                f"UCataclysmPlayerClassStats::StatToAttribute nor "
                f"StatsWithNoAttribute, so the node would grant nothing")

        for tag in (t.strip() for t in row["RequiredTags"].split(",")):
            if tag and tag not in known:
                problems.append(
                    f"PassiveEffects/{row['Name']}: undefined tag {tag}")

        # THE CAPSTONE OPTION COLUMN, WHICH FAILS SILENTLY IN BOTH DIRECTIONS.
        # Issue #1029.
        #
        # AN OPTION ON A NODE THAT IS NOT A CAPSTONE grants nothing at all:
        # `AccumulateInto` compares it against `ChosenOptionIn`, which answers 0
        # for such a node, so the row is skipped every time and the node reads as
        # unauthored.
        #
        # A CAPSTONE ROW WITH NO OPTION applies whichever of the three the player
        # picked, which is the opposite failure and the worse one: the player is
        # given part of an option they refused. Refusing it here is the only
        # place that can be caught. If a capstone ever legitimately wants a row
        # that applies whatever is chosen, this is the check to relax, and the
        # relaxation should say which node needed it.
        # A THRESHOLD THE NODE CANNOT REACH GRANTS NOTHING, EVER. Issue #1755.
        # "At 4 points" on a node that holds three is a row nobody can earn, and
        # nothing at run time says so.
        min_points = int(row.get("MinPoints", 0) or 0)
        most = max_points.get(row["Node"])
        if min_points and most is not None and min_points > most:
            problems.append(
                f"PassiveEffects/{row['Name']}: applies from {min_points} "
                f"points and {row['Node']} holds at most {most}, so no player "
                f"can ever earn it")

        option = int(row.get("Option", 0) or 0)
        is_capstone = row["Node"] in capstones
        if option and not is_capstone:
            problems.append(
                f"PassiveEffects/{row['Name']}: names capstone option {option} "
                f"and {row['Node']} is not a capstone, so the row would be "
                f"skipped every time and grant nothing")
        elif is_capstone and not option and not capstone_states_a_clause(
                capstone_sentences.get(row["Node"], "")):
            problems.append(
                f"PassiveEffects/{row['Name']}: {row['Node']} is a capstone and "
                f"the row names no option, so it would apply whichever of the "
                f"three the player picked. Only a capstone whose own sentence "
                f"states a clause beside its choice may carry such a row.")

    # AND A SCALE ONLY WHERE SOMETHING ASKS FOR THE STAT. Issue #1973: a
    # scaled row on a stat nothing asks for is accepted, built, imported and
    # dead, and this is the last moment anybody looks at it.
    problems.extend(refuse_a_scale_nothing_asks_for("PassiveEffects", effects))
    problems.extend(refuse_a_condition_nothing_asks_for("PassiveEffects", effects))

    # AND NO NODE GRANTS CLASS POINTS, which the game reads from worn rows only.
    problems.extend(
        refuse_a_class_point_row_the_game_cannot_count("PassiveEffects", effects))

    # AND NO ACTION ON A KILL ASKS THE KILL'S OWN CLOCK, where it always holds.
    # Ruled 2026-11-25 (the Death trees' slice C).
    problems.extend(refuse_a_kill_clock_on_a_kill_action("PassiveEffects", effects))

    return problems


def _stats_with_a_base(tables: dict[str, list[dict]]) -> set[str]:
    """Every stat a class line, an attribute, an item base or the engine supplies.

    THE SET `validate_passive_effects` BUILDS, LESS THAT SHEET'S OWN FLAT ROWS.
    That function still builds it inline; moving it onto this helper waits for
    the passive-tree work on another branch to merge, for the reason
    `_condition_and_scale` gives.

    SO THE TWO HAVE TO BE WIDENED TOGETHER, which is why the exempt stats are
    here as well as there. Issue #1733. Widening only the passive one would make
    the sentence above false and would leave an enchantment naming a minion stat
    refused for a reason that no longer applies to the identical passive row.
    """
    class_rows = tables.get("ClassStats")
    attribute_rows = tables.get("Attributes")
    return (({row["Stat"] for row in class_rows} if class_rows else set())
            | ({row["Stat"] for row in attribute_rows}
               if attribute_rows else set())
            | item_base_flat_stats(tables.get("ItemBases"))
            | item_base_column_stats(tables.get("ItemBases"))
            | set(ENGINE_SUPPLIED_BASES)
            | stats_with_no_attribute())


def validate_enchantment_effects(tables: dict[str, list[dict]],
                                 known: set[str]) -> list[str]:
    """Every enchantment effect that changes a stat names one something
    supplies, and every effect names real tags.

    BOTH FAIL SILENTLY WITHOUT THIS, as they do for a passive effect: an increase
    on a stat nothing supplies multiplies a base of zero, and a required tag
    nobody declared matches no skill, so the modifier applies to nothing.

    A `flat` ROW IN THIS SHEET SUPPLIES ITS OWN STAT, for the reason
    `validate_passive_effects` gives: the complaint is an increase with no base,
    and a flat row is a base. "Retaliation damage applies to all enemies within
    3 meters" is the case: nothing else supplies the retaliation radius.

    WHICH ENCHANTMENT A ROW NAMES, AND WHETHER ITS WORDS AGREE, IS CHECKED WHEN
    THE SHEET IS READ, in `enchantment_effects`, because that is where the
    Enchantments sheet is in hand.

    WHAT STILL CATCHES A STAT WITH NO ATTRIBUTE BEHIND IT IS THE ENGINE SIDE.
    `Cataclysm.Enchantments.EveryStatAnEnchantmentGrantsHasAnAttributeBehindIt`
    fails on it, which matters because `UCataclysmPlayerClassStats::ApplyTo`
    drops such a stat in silence.
    """
    effects = tables.get("EnchantmentEffects")
    if not effects:
        return []

    # AND A FLAT ROW IN THE PASSIVE SHEET, since issue #1833's rows-only batch.
    # "Your class resource decays twice as fast" doubles the Fervour decay, and
    # the one thing that supplies a decay rate is a passive node's flat row,
    # `Ravager_basic_spine_000` at 5 a second. A doubling is a multiplier on
    # that base, which is the case this check exists to allow.
    passives = tables.get("PassiveEffects") or []
    stats = _stats_with_a_base(tables) | {
        row["Stat"] for row in (*effects, *passives)
        if str(row["ValueKind"]).lower() == "flat"}

    problems = []
    for row in effects:
        # A ROW THAT MOVES A POOL HAS NO STAT, BY DESIGN. `enchantment_effects`
        # refuses a row naming both a stat and a pool, and `_check_pool_action`
        # checks its pool against the ones the game has. Asking it for a stat
        # with a base refused every such row, which nothing showed until the
        # eight rows of issue #1815 were run through the whole generator on
        # 2026-09-16. Its required tags are still checked below.
        #
        # AND A REMOVAL NEEDS NOTHING UNDER IT. Issue #1791. The complaint is an
        # increase multiplying a base of zero, and a removal multiplies nothing:
        # it takes the stat to zero from whatever supplies it, the rolled affixes
        # this set leaves out included. "You have no resistances." is eight such
        # rows, and a player's resistances come from affixes alone, so asking it
        # for a base refused all eight when the rows were run through the whole
        # generator on 2026-09-16.
        if (not row.get("Action")
                and str(row["ValueKind"]).lower() != "removed"
                and row["Stat"] not in stats):
            problems.append(
                f"EnchantmentEffects/{row['Name']}: {row['Stat']!r} is not a "
                f"stat any class line, attribute, item base or engine base "
                f"supplies, and no flat row in this sheet supplies it either")

        # AND A FLAT ROW ON A STAT THE GAME READS NOWHERE. Issue #2387. The check
        # above asks whether an increase has a base under it, and a flat row IS
        # a base, so it supplies its own stat there and was never asked whether
        # the game reads that stat at all. The game reads a stat row only for a
        # name in `StatToAttribute` or `StatsWithNoAttribute`; any other name is
        # dropped by `ApplyTo` without a word, so a misspelt flat row, or one
        # whose mechanism has not merged, would be written and do nothing.
        if (not row.get("Action")
                and str(row["ValueKind"]).lower() == "flat"
                and row["Stat"] not in stats_with_an_attribute()
                and row["Stat"] not in stats_with_no_attribute()):
            problems.append(
                f"EnchantmentEffects/{row['Name']}: {row['Stat']!r} is a flat "
                f"row on a stat the game reads nowhere: it is in neither "
                f"UCataclysmPlayerClassStats::StatToAttribute nor "
                f"StatsWithNoAttribute, so the row would be written and do "
                f"nothing")

        for tag in (t.strip() for t in str(row["RequiredTags"]).split(",")):
            if tag and tag not in known:
                problems.append(
                    f"EnchantmentEffects/{row['Name']}: undefined tag {tag}")

    # AND THE SAME REFUSAL AS THE PASSIVE SHEET, for the same reason: the two
    # sheets share the scale vocabulary, so a dead scaled row is as easy to
    # write here as there. Issue #1973.
    problems.extend(refuse_a_scale_nothing_asks_for("EnchantmentEffects", effects))
    problems.extend(
        refuse_a_condition_nothing_asks_for("EnchantmentEffects", effects))

    # AND A SCALE THAT COUNTS ENEMIES IN A RADIUS THIS SHEET CANNOT STATE.
    # Issue #1987. The refusal above asks whether anything asks for the row's
    # STAT, so it passes a row scaling a live stat by a reach this sheet has no
    # column for -- which is accepted, built, imported and dead.
    problems.extend(
        refuse_a_reach_scale_a_row_cannot_state("EnchantmentEffects", effects))

    # AND A ROW GRANTING CLASS POINTS IS ONE THE GAME'S ONE READER COUNTS.
    # Ruled 2026-10-09.
    problems.extend(refuse_a_class_point_row_the_game_cannot_count(
        "EnchantmentEffects", effects))

    # AND A ROW ON A HEALING SKILL STAT IS ONE ITS READER GIVES ITS SENTENCE'S
    # MEANING. Ruled 2026-10-09.
    problems.extend(refuse_a_healing_skill_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND A ROW ON EITHER STAT OF AN IMMUNITY TO THE DAMAGE TYPE THAT HIT IS ONE
    # THE GAME OPENS A WINDOW FOR. Ruled 2026-10-12.
    problems.extend(refuse_a_damage_type_immunity_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND A ROW ON THE FLAG THAT RETALIATION CANNOT BE MITIGATED IS ONE THE
    # GAME ANSWERS YES TO. Ruled 2026-10-12.
    problems.extend(refuse_a_retaliation_flag_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND A ROW ON THE SECONDS AFTER WHICH AN ENEMY IS IMMUNE TO ITS WEARER IS
    # ONE FIGURE THE GAME READS, AND STATES NOTHING OF THE 3 SECONDS. Ruled
    # 2026-10-12.
    problems.extend(refuse_an_enemies_immune_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE SHARE OF DAMAGE TAKEN THAT IS DELAYED AND ITS SECONDS ARE TWO
    # FLAT ROWS OF ONE ENCHANTMENT, EACH STATED ONCE. Ruled 2026-10-12.
    problems.extend(refuse_a_delayed_damage_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND A ROW ON A STAT THAT MULTIPLIES A FIGURE OF A SKILL OF ITS WEARER'S
    # IS ONE ITS READER GIVES ITS SENTENCE'S MEANING. Ruled 2026-10-13.
    problems.extend(refuse_an_aura_or_buff_effect_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE PERCENT LESS A DEBUFFED ENEMY DEALS IS ONE FLAT ROW WITH NO TAGS.
    # Ruled 2026-10-13.
    problems.extend(refuse_a_debuff_cut_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE FLAG THAT MAKES A WEARER IMMUNE TO DEBUFFS AND UNABLE TO LAY ANY
    # IS FLAT, 1 TO 1, WITH NO TAGS. Ruled 2026-10-13.
    problems.extend(refuse_a_debuff_immunity_flag_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE CHANCE THAT AN AILMENT EXPLODES WHEN IT ENDS BY TIME IS ONE FLAT
    # ROW THAT NAMES ITS AILMENT BY ITS ONE REQUIRED TAG. Ruled 2026-10-13.
    problems.extend(refuse_an_exploding_ailment_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND A ROW ON A STAT A SUMMONER'S GADGETS ARE ASKED FOR IS FLAT AND
    # SCOPED TO `Type.Deployable`. Ruled 2026-10-13.
    problems.extend(refuse_a_gadget_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE FLAG THAT TAKES THE ULTIMATE'S COOLDOWN AWAY AND DOUBLES ITS COST
    # IS FLAT, 1 TO 1, WITH NO TAGS AND NO ACTION. Ruled 2026-10-14.
    problems.extend(refuse_an_ultimate_cost_doubling_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE SHARE OF WHAT REACHES A NEARBY ALLY'S HEALTH IS FLAT, ABOVE 0 AND
    # AT MOST 100, WITH NO TAGS AND NO ACTION. Ruled 2026-10-15.
    problems.extend(refuse_a_share_of_allies_damage_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE SHARE OF A POINT-BLANK SKILL'S BLOW STRUCK AT ALLIES IS FLAT,
    # ABOVE 0 AND AT MOST 100, REQUIRES Type.AOE.PointBlank, AND HAS NO ACTION.
    # Ruled 2026-10-15.
    problems.extend(
        refuse_a_point_blank_share_on_allies_row_the_game_cannot_read(
            "EnchantmentEffects", effects))

    # AND THE FLAG THAT CONVERTS A SLOT INTO A SKILL GADGET IS FLAT, 1 TO 1,
    # REQUIRES ONE SLOT'S TAG AND NOTHING ELSE, AND HAS NO ACTION. Ruled
    # 2026-10-16.
    problems.extend(refuse_a_skill_slot_gadget_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE CHANCE THE VOID AURA CONSUMES AN ENEMY'S SHOT IS FLAT, ABOVE 0
    # AND AT MOST 100, WITH NO TAGS AND NO ACTION. Ruled 2026-10-17 (O9).
    problems.extend(refuse_a_void_aura_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND A ROW COUNTING ENEMIES ONLY WHILE ANOTHER ROW'S STACK IS HELD HAS
    # THAT ROW BESIDE IT. Ruled by the project owner on 2026-10-17.
    problems.extend(refuse_a_window_scale_with_no_own_stack_row(
        "EnchantmentEffects", effects))

    # AND NO ACTION ON A KILL ASKS THE KILL'S OWN CLOCK, where it always holds.
    # Ruled 2026-11-25 (the Death trees' slice C).
    problems.extend(refuse_a_kill_clock_on_a_kill_action(
        "EnchantmentEffects", effects))

    # AND THE DEATH FLAG AND THE TYRANT'S MINION'S SECONDS ARE FLAT, WITH NO
    # TAGS AND NO ACTION; THE FLAG IS 1 AND THE SECONDS ABOVE 0. Ruled
    # 2026-10-18.
    problems.extend(refuse_a_tyrants_minion_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE THREE FIGURES OF A REPRIEVE FROM A LETHAL BLOW ARE FLAT, WITH NO
    # TAGS AND NO ACTION; THE INTERVAL SCALED ONLY BY ITS OWN STACKS; THE RUNG
    # ONE WHOLE RUNG FROM 1 TO 5. Ruled 2026-10-18.
    problems.extend(refuse_a_lethal_hit_reprieve_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE FIVE GROUND POUND STATS ARE FLAT, WITH NO TAGS AND NO ACTION, AND
    # ABOVE 0. Ruled 2026-10-17 (#5) and 2026-10-18.
    problems.extend(refuse_a_ground_pound_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE FOUR WARHOUND STATS ARE FLAT, WITH NO TAGS AND NO ACTION, AND
    # ABOVE 0. Ruled 2026-10-17 (#6) and 2026-10-18.
    problems.extend(refuse_a_warhound_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE FOUR TAUNT AURA STATS ARE FLAT, WITH NO TAGS AND NO ACTION, AND
    # ABOVE 0. Ruled 2026-10-17 (#6) and 2026-10-18.
    problems.extend(refuse_a_taunt_aura_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE TYRANT'S MINION'S SECONDS BETWEEN ULTIMATE CASTS ARE FLAT, WITH NO
    # TAGS AND NO ACTION, ABOVE 0 AND NOT RISING. Ruled 2026-10-17 (#7) and
    # 2026-10-18 (T1-T7).
    problems.extend(refuse_a_tyrants_ultimate_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE FLAG BY WHICH A MINION'S DEATH LEAVES A SOUL IS FLAT, 1 AND NOTHING
    # ELSE, WITH NO TAGS AND NO ACTION. Ruled 2026-10-17 (#8) and 2026-10-18
    # (R1-R8).
    problems.extend(refuse_a_reaper_soul_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE CULL PER STACK OF SOUL STRIKE IS FLAT, ABOVE 0 AND AT MOST 100, AND
    # THE FLAG BY WHICH A KILL SPAWNS A MINION IS FLAT, 1 AND NOTHING ELSE, EACH
    # WITH NO TAGS AND NO ACTION. Ruled 2026-10-17 (#9) and 2026-10-19 (C1-C8).
    problems.extend(refuse_a_reaper_ten_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE SEVEN FIGURES OF THE TEMPORAL RIFT ARE FLAT, WITH NO TAGS AND NO
    # ACTION, ABOVE 0, THE SECONDS BETWEEN RIFTS NOT RISING. Ruled 2026-10-08,
    # 2026-10-17 (#10) and 2026-10-19 (K1-K9).
    problems.extend(refuse_a_time_lock_rift_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE FLAG BY WHICH ITS HOLDER REFUSES BUFFS FROM ALLIES IS FLAT, 1 TO
    # 1, WITH NO TAGS AND NO ACTION. Ruled 2026-10-19 (A1).
    problems.extend(refuse_an_ally_buffs_refused_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE FLAG BY WHICH A WEARER'S OWN DEPLOYABLE'S BLAST STRIKES THE WEARER
    # IS FLAT, 1 AND NOTHING ELSE, WITH NO TAGS AND NO ACTION. Ruled 2026-10-19
    # (B1-B6).
    problems.extend(refuse_a_deployable_self_blast_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    # AND THE FLAG BY WHICH A SLOT'S PRESS PLACES A SKILL TRAP IS FLAT, 1 TO 1,
    # REQUIRING ONE BUILT SLOT'S TAG; AND THE TRAP'S TWO INCREASES ARE
    # INCREASES, REQUIRING NOTHING OR `Type.Trap`. Ruled 2026-10-19 (T1-T19).
    problems.extend(refuse_a_skill_slot_trap_row_the_game_cannot_read(
        "EnchantmentEffects", effects))
    problems.extend(refuse_a_trap_increase_row_the_game_cannot_read(
        "EnchantmentEffects", effects))
    # AND THE NO-EXPIRE FLAG IS FLAT, REQUIRING NOTHING OR `Type.Trap`, A FLAG
    # OF 1. Ruled 2026-10-21 (M2).
    problems.extend(refuse_a_trap_expiry_row_the_game_cannot_read(
        "EnchantmentEffects", effects))
    # AND THE RESERVE PERCENTAGE IS FLAT, REQUIRING NOTHING, `Type.Deployable`
    # OR `Type.Trap`, WITHIN ITS BOUND AND OF ONE SIGN. Issue #2787, branch B.
    problems.extend(refuse_a_reserve_percent_row_the_game_cannot_read(
        "EnchantmentEffects", effects))
    # AND THE FOUR STATS OF A TRAP'S BLOW EACH STATE THEIR ONE KIND, REQUIRING
    # NOTHING OR `Type.Trap`, WITHIN THEIR BOUNDS. Ruled 2026-10-21 (F1 to F9).
    problems.extend(refuse_a_trap_blow_row_the_game_cannot_read(
        "EnchantmentEffects", effects))
    # AND THE TRAP'S PULL RADIUS IS FLAT, REQUIRING NOTHING OR `Type.Trap`,
    # WITHIN ITS BOUND. Ruled 2026-10-22 (J7, J8).
    problems.extend(refuse_a_trap_pull_row_the_game_cannot_read(
        "EnchantmentEffects", effects))
    # AND THE COUNT OF DAMAGE TYPES A WEAPON'S ENCHANTMENT ADDS IS FLAT, A WHOLE
    # NUMBER FROM 1 TO 8, WITH NOTHING ELSE STATED. Ruled 2026-10-25 (Q-D5).
    problems.extend(refuse_a_damage_types_carried_row_the_game_cannot_read(
        "EnchantmentEffects", effects))
    # AND THE TWO GEM ENCHANTMENTS' ROWS ARE THE SHAPES THE GEM CODE READS.
    # Issue #46 slice G10, ruled 2026-11-22.
    problems.extend(refuse_a_gem_enchantment_row_the_game_cannot_read(
        "EnchantmentEffects", effects))

    return problems

TABLES = {
    "DungeonModifiers": dungeon_modifiers,
    "WeaponSkills": weapon_skills,
    "EnchantmentsPositive": lambda b: enchantments(b, negative=False),
    "EnchantmentsNegative": lambda b: enchantments(b, negative=True),
    "EnemyModifiers": enemy_modifiers,
    "StatusEffects": status_effects,
    "Gems": gems,
    "CityUpgrades": city_upgrades,
    "CraftingMaterials": crafting_materials,
    "ForgeOperations": forge_operations,
    "ItemBases": item_bases,
    "Affixes": affixes,
    "GearRarity": gear_rarity,
    "ItemSockets": item_sockets,
    "AffixTiers": affix_tiers,
    "EnemyDrops": enemy_drops,
    "MaterialTiers": material_tiers,
    "Gold": gold,
    "ClassStats": class_stats,
    "MinionTypes": minion_types,
    "MinionScaling": minion_scaling,
    "Attributes": attributes,
    "PassiveEffects": passive_effects,
    "EnchantmentEffects": enchantment_effects,
    "SkillSlots": skill_slots,
    "ElementVisuals": element_visuals,
    "WeaponMeshes": weapon_meshes,
}

#: Tables built from sim/cataclysm_sim/enemy_stats.py rather than the workbook.
#: Kept apart from TABLES only so a stale-file message can name the right source;
#: everything downstream treats the two the same.
MODEL_TABLES = {
    "EnemyArchetypes": enemy_archetypes,
    "EnemyRarities": enemy_rarities,
}

#: Built from the tree node graphs in `docs/`, which are exported from a
#: separate authoring tool: the class trees, and since issue #2741 the empire
#: upgrade tree. A third source beside the workbook and the Python model;
#: everything after this point treats all three alike.
TREE_TABLES = {
    "PassiveNodes": passive_nodes,
    "PassiveEdges": passive_edges,
    "EmpireTreeNodes": empire_tree_nodes,
    "EmpireTreeEdges": empire_tree_edges,
    "EmpireTreeEffects": empire_tree_effects,
}


# --------------------------------------------------------------------------

def render_csv(rows: list[dict]) -> str:
    if not rows:
        raise DataError("no rows to write")
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, fieldnames=list(rows[0].keys()),
                            lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    return buffer.getvalue()


def declared_tags(book) -> set[str]:
    """Exactly what the Tags sheet declares, with no implied parents."""
    return {clean(r[0]) for r in book["Tags"].iter_rows(values_only=True)
            if r and clean(r[0])} - {"Tag Name"}


def known_tags(book) -> set[str]:
    """Every declared tag plus the parents Unreal creates implicitly."""
    known: set[str] = set()
    for tag in declared_tags(book):
        parts = tag.split(".")
        for i in range(1, len(parts) + 1):
            known.add(".".join(parts[:i]))
    return known


def validate_tags(tables: dict[str, list[dict]], known: set[str]) -> list[str]:
    problems = []
    for table, rows in tables.items():
        for row in rows:
            for tag in (t.strip() for t in row.get("Tags", "").split(",")):
                if tag and tag not in known:
                    problems.append(f"{table}/{row['Name']}: undefined tag {tag}")
    return problems


def validate_affix_slots(tables: dict[str, list[dict]]) -> list[str]:
    """Every slot an affix allows must be a slot some item base occupies.

    A misspelled slot does not fail: the affix simply never rolls, on any drop,
    and nothing reports it. Cross-checking the two sheets against each other
    catches it without either needing a hard-coded list of slots, so adding a
    slot to the design needs no change here.
    """
    bases = tables.get("ItemBases")
    affix_rows = tables.get("Affixes")
    if not bases or not affix_rows:
        return []

    real = {row["Slot"] for row in bases}
    problems = []
    for row in affix_rows:
        for slot in (s.strip() for s in row["AllowedSlots"].split(",")):
            if slot and slot not in real:
                problems.append(
                    f"Affixes/{row['Name']}: allows slot {slot!r}, which no "
                    f"item base occupies")

    # And the other way: a slot with no affix at all could fill none of its
    # four slots, which the affix pool is required to avoid.
    reachable = {s.strip() for row in affix_rows
                 for s in row["AllowedSlots"].split(",") if s.strip()}
    for slot in sorted(real - reachable):
        problems.append(f"ItemBases: slot {slot!r} can roll no affix at all")

    return problems



def validate_socket_slots(tables: dict[str, list[dict]]) -> list[str]:
    """Every gear slot an item base occupies needs a socket maximum, and no
    socket maximum may name a slot no base occupies.

    Neither direction fails anywhere else. A slot missing from the Item Sockets
    sheet leaves a drop with nothing to say how many sockets the piece can hold;
    a slot in that sheet which nothing occupies is a maximum no drop will ever
    read.

    MATCHED ON THE SLOT AND THE HAND COUNT TOGETHER, because a weapon's maximum
    depends on how many hands it takes and the two hand counts are separate rows
    in both sheets.
    """
    bases = tables.get("ItemBases")
    sockets = tables.get("ItemSockets")
    if not bases or not sockets:
        return []

    occupied = {(row["Slot"], row["Hands"]) for row in bases}
    stated = {(row["Slot"], row["Hands"]) for row in sockets}

    problems = []
    for slot, hands in sorted(occupied - stated):
        problems.append(
            f"ItemSockets: nothing states a socket maximum for {slot} at "
            f"{hands} hand(s), which item bases occupy")
    for slot, hands in sorted(stated - occupied):
        problems.append(
            f"ItemSockets: a socket maximum for {slot} at {hands} hand(s), "
            "which no item base occupies")
    return problems



def validate_enemy_drop_rarities(tables: dict[str, list[dict]]) -> list[str]:
    """The Enemy Drops sheet and the enemy rarity ladder must name the same
    rarities, in the same order.

    THE TWO COME FROM DIFFERENT PLACES, which is why this is worth checking.
    `EnemyRarities.csv` is generated from `sim/cataclysm_sim/enemy_stats.py`,
    whose ladder is in turn a port of the external DungeonSimulator power model.
    `EnemyDrops.csv` is typed into the workbook. A rarity added to one and not
    the other is a rarity that either drops nothing or drops for a creature that
    cannot exist, and nothing else would report it.

    ORDER TOO, NOT ONLY MEMBERSHIP, because both tables carry a Step column
    saying where a rarity sits and the two have to agree about that as well.
    """
    drops = tables.get("EnemyDrops")
    rarities = tables.get("EnemyRarities")
    if not drops or not rarities:
        return []

    problems = []
    dropping = [row["Name"] for row in drops]
    known = [row["Name"] for row in rarities]
    if dropping != known:
        problems.append(
            f"EnemyDrops names {dropping} and EnemyRarities names {known}. "
            "They have to be the same rarities in the same order; the ladder "
            "comes from the simulation's enemy model and is not the workbook's "
            "to change.")
        return problems

    for drop, rarity in zip(drops, rarities, strict=True):
        if drop["Step"] != rarity["Step"]:
            problems.append(
                f"EnemyDrops/{drop['Name']}: Step {drop['Step']} against "
                f"{rarity['Step']} in EnemyRarities")
    return problems


#: `Tier 3 (Rare). Drop from Dungeon Bosses/Elites.` -- the tier and its name.
MATERIAL_TIER_AND_SOURCE = re.compile(r"Tier\s*(\d)\s*\(([^)]+)\)")

#: `Upgrade Stone +7` -- the level the stone takes a piece of gear to.
UPGRADE_STONE_NAME = re.compile(r"Upgrade\s+Stone\s*\+(\d+)$")

#: The highest a piece of gear goes. Section VII of the design document states
#: it twice: the Upgrade Item Level operation is "1 per tier, maximum 10", and
#: the stone that reaches it "Raises an item to +10, which is the maximum".
#: Nothing above that can be crafted, so nothing above it can drop.
MAX_UPGRADE_STONE_LEVEL = 10


def validate_material_tier_counts(tables: dict[str, list[dict]]) -> list[str]:
    """Every material tier's stated count must match how many materials it has.

    THE COUNT IS NOT DECORATION. How often a NAMED top-tier material drops is
    the tier's share divided by this number, and Purified Essence -- the only
    thing that clears the Consumption Threshold -- is one of three that share
    the top tier. A wrong count here makes that figure wrong and nothing else
    notices.

    Counted off the Crafting sheet through `CraftingMaterials`, which is where
    the materials actually are, rather than trusting either restatement.
    """
    tiers = tables.get("MaterialTiers")
    materials = tables.get("CraftingMaterials")
    if not tiers or not materials:
        return []

    counted: dict[str, int] = {}
    for row in materials:
        found = MATERIAL_TIER_AND_SOURCE.match(row["TierAndSource"])
        if found:
            counted[found.group(2)] = counted.get(found.group(2), 0) + 1

    if not counted:
        return ["MaterialTiers: no 'Tier N (Name)' cell could be read out of "
                "CraftingMaterials, so the counts could not be checked at all"]

    problems = []
    for row in tiers:
        real = counted.get(row["TierName"])
        if real is None:
            problems.append(
                f"MaterialTiers/{row['Name']}: no crafting material is in a "
                f"tier called {row['TierName']!r}")
        elif real != row["Materials"]:
            problems.append(
                f"MaterialTiers/{row['Name']}: says {row['Materials']} "
                f"materials, and CraftingMaterials has {real}")

    for name in sorted(set(counted) - {row["TierName"] for row in tiers}):
        problems.append(
            f"MaterialTiers: CraftingMaterials has {counted[name]} material(s) "
            f"in a tier called {name!r}, and MaterialTiers has no such row")
    return problems


def validate_upgrade_stone_levels(tables: dict[str, list[dict]]) -> list[str]:
    """The ten upgrade stones must publish levels +1 to +10, one stone each.

    THE LEVEL IS DERIVED FROM THE NAME, so a rename in the workbook silently
    turns it into a zero rather than into an error. A zero reads as "not a
    stone", and `UCataclysmDropRoll::RollMaterial` caps what may drop by that
    level -- so a stone with a zero would be droppable at every difficulty tier,
    which is the exact bug #863 is about, arriving the other way round.

    Checked as a set rather than as a count. Two stones both named "+7" would
    keep the count at ten and put a hole in the ladder.
    """
    materials = tables.get("CraftingMaterials")
    if not materials:
        return []

    levels: dict[int, list[str]] = {}
    for row in materials:
        if row["UpgradeLevel"]:
            levels.setdefault(row["UpgradeLevel"], []).append(row["MaterialName"])

    if not levels:
        return ["CraftingMaterials: no material is named 'Upgrade Stone +N', so "
                "no upgrade stone can drop at all. The ten stones were renamed "
                "or removed, or UPGRADE_STONE_NAME no longer matches them."]

    problems = []
    for level in range(1, MAX_UPGRADE_STONE_LEVEL + 1):
        if level not in levels:
            problems.append(
                f"CraftingMaterials: nothing upgrades gear to +{level}. Gear "
                f"runs +1 to +{MAX_UPGRADE_STONE_LEVEL} and each step needs a "
                f"stone.")
        elif len(levels[level]) > 1:
            problems.append(
                f"CraftingMaterials: {len(levels[level])} materials upgrade "
                f"gear to +{level}: {sorted(levels[level])}")
    return problems


#: The one weapon type in the Weapon Skills sheet that is not a weapon. It marks
#: a skill that does not depend on which weapon is held, which is how the single
#: Aura skill is written: one aura per damage type rather than one per weapon.
WEAPON_INDEPENDENT_SKILL = "All"


def validate_weapon_skill_types(tables: dict[str, list[dict]]) -> list[str]:
    """Every weapon type in the Weapon Skills sheet must be a real weapon base.

    The two sheets held different names for the same three weapons: the skills
    sheet said "2H Axe", "2h Sword" and "2H Warhammer" where the item bases sheet
    and the design document's weapon table said Greataxe, Greatsword and
    Warhammer. Nothing reported it, and a lookup keyed on weapon type would have
    found no skills at all for five of the fourteen bases.

    Cross-checked against the other sheet rather than a list written here, so
    adding a weapon to the design needs no change in this file.

    IT ONLY EVER LOOKED AT THE `WeaponType` COLUMN, which is why the same rename
    survived in the gameplay tags. `validate_weapon_tags` below is the other
    half. Issue #620.
    """
    bases = tables.get("ItemBases")
    skills = tables.get("WeaponSkills")
    if not bases or not skills:
        return []

    real = {row["WeaponType"] for row in bases if row["WeaponType"]}
    problems = []
    for name in sorted({row["WeaponType"] for row in skills if row["WeaponType"]}):
        if name != WEAPON_INDEPENDENT_SKILL and name not in real:
            problems.append(
                f"WeaponSkills: weapon type {name!r} is not a weapon base. The "
                f"bases are {sorted(real)}.")

    # And the other way. A weapon with no rows at all can never have a skill in
    # any slot, which is a hole rather than a design choice.
    covered = {row["WeaponType"] for row in skills}
    for name in sorted(real - covered):
        problems.append(f"WeaponSkills: the {name} has no rows at all")

    return problems


def validate_weapon_skill_damage_types(tables: dict[str, list[dict]],
                                       declared: set[str]) -> list[str]:
    """Every damage type in the Weapon Skills sheet must be one somebody has.

    THE SAME SHAPE OF FAILURE `validate_weapon_skill_types` ABOVE WAS WRITTEN
    FOR, one column over, and until issue #579 the Damage Type column was checked
    by nothing at all. `UCataclysmWeaponSkills::SkillsFor` in the engine matches
    it with `Row.DamageType.Equals(DamageType, ESearchCase::IgnoreCase)`, an exact
    comparison, so a row naming a damage type nobody has is offered to nobody: it
    generates cleanly, imports cleanly, fills no slot and produces no error. A
    single misspelling costs one skill and says nothing.

    THE WILDCARD CASE IS THE ONE THAT LOOKS LIKE IT SHOULD WORK. The WEAPON axis
    has one -- `UCataclysmWeaponSkills::WeaponIndependent` is `All`, and the eight
    Aura rows use it, because an aura is not a property of the weapon being held.
    The damage type axis has no such thing, so `All` in this column is dead data
    that reads like a feature. Refused by name below, with the reason, because
    somebody writing it has guessed at symmetry that is not there.

    NOTHING IN THE SHEET USES `All` TODAY and nothing needs to. Issue #579 was
    filed when the basic attack was expected to live in this matrix as fourteen
    damage-type-independent rows; issue #524 settled it the other way, and the
    basic attack now comes from the weapon base through
    `UCataclysmWeaponSkills::BasicAttackFor`. So the wildcard is not built,
    because building it would mean choosing a precedence rule between four
    classes of row with no data to check the choice against.

    Cross-checked against the Tags sheet rather than a list written here, so
    adding a ninth damage type to the design needs no change in this file.
    """
    skills = tables.get("WeaponSkills")
    if not skills:
        return []

    real = {tag[len(ELEMENT_TAG_PREFIX):] for tag in declared
            if tag.startswith(ELEMENT_TAG_PREFIX)}
    if not real:
        return []

    problems = []
    for name in sorted({row["DamageType"] for row in skills if row["DamageType"]}):
        if name in real:
            continue
        if name == WEAPON_INDEPENDENT_SKILL:
            problems.append(
                f"WeaponSkills: damage type {name!r} is not a wildcard. The "
                f"WeaponType column has one and this column does not: "
                f"UCataclysmWeaponSkills::SkillsFor matches the damage type "
                f"exactly, so these rows would be granted to nobody. Issue #579 "
                f"records what building one would take. Name one of "
                f"{sorted(real)} instead.")
        else:
            problems.append(
                f"WeaponSkills: damage type {name!r} is not declared on the Tags "
                f"sheet. A row naming a damage type nobody has is granted to "
                f"nobody and reports nothing. The declared ones are "
                f"{sorted(real)}.")

    # And the other way. A damage type with no rows at all is a Cataclysm whose
    # characters have no skills, which is a hole rather than a design choice.
    covered = {row["DamageType"] for row in skills}
    for name in sorted(real - covered):
        problems.append(
            f"WeaponSkills: the {name} damage type has no rows at all")

    return problems


#: The prefix every weapon type's gameplay tag carries. Declared on the Tags
#: sheet and generated into game/Config/Tags/CataclysmTags.ini.
WEAPON_TAG_PREFIX = "Item.Weapon."


def weapon_tag_leaf(weapon_type: str) -> str:
    """The tag leaf a weapon type must use, from the type's own name.

    ONE RULE, AND IT IS ONLY ABOUT SPACES. A gameplay tag cannot contain a space
    and one weapon type does: "2H Crossbow" in `game/Data/ItemBases.csv`. So the
    leaf is the weapon type with its spaces removed and nothing else changed,
    which makes `Item.Weapon.2HCrossbow` the only legal spelling for it and
    leaves the other thirteen identical to their weapon type.
    """
    return weapon_type.replace(" ", "")


def validate_weapon_tags(tables: dict[str, list[dict]],
                         declared: set[str]) -> list[str]:
    """Every weapon type's tag is named after the weapon. Issue #620.

    WHY THIS EXISTS. Three weapons carried a tag named after what they used to be
    called: a Greataxe's rows said `Item.Weapon.2hAxe`, a Greatsword's said
    `Item.Weapon.2hSword`, a Warhammer's said `Item.Weapon.2hWarhammer`, and the
    2H Crossbow's differed in letter case. That was the tail of a rename which
    corrected the `WeaponType` column and left the tags behind.

    NOTHING WAS BROKEN BY IT, which is exactly why it survived. The naming was
    consistent within the data, so every lookup worked and no test failed. What
    it cost was a reader having to know two names for one weapon.

    `validate_weapon_skill_types` above is the same shape of check for the
    `WeaponType` column, and it is what caught the first half of that rename. It
    could not catch this half because it never looks at a tag.

    BOTH DIRECTIONS. A weapon with no tag cannot be scoped to by an affix or a
    passive, and a weapon tag naming no weapon is a name for something that does
    not exist.
    """
    bases = tables.get("ItemBases")
    if not bases:
        return []

    weapons = {row["WeaponType"] for row in bases if row["WeaponType"]}
    wanted = {WEAPON_TAG_PREFIX + weapon_tag_leaf(name) for name in weapons}
    have = {tag for tag in declared if tag.startswith(WEAPON_TAG_PREFIX)}

    problems = [
        f"Tags: {tag} is not declared, and it is the tag for a weapon type in "
        f"the Item Bases sheet. A weapon with no tag cannot be scoped to."
        for tag in sorted(wanted - have)
    ]
    problems += [
        f"Tags: {tag} names no weapon type. The Item Bases sheet's weapon types "
        f"are {sorted(weapons)}, so the tags are {sorted(wanted)}."
        for tag in sorted(have - wanted)
    ]
    return problems


def validate_skill_effects(tables: dict[str, list[dict]]) -> list[str]:
    """A skill's Effect must name a status effect that exists.

    An Effect nobody defined generates cleanly and grants nothing at runtime,
    because the tag it would use is never declared: the skill runs, spends its
    mana, waits its cooldown and applies no debuff. Cross-checked against the
    Buffs, Debuffs and DoTs sheets rather than a list written here, so adding an
    effect to the design needs no change in this file.
    """
    skills = tables.get("WeaponSkills")
    effects = tables.get("StatusEffects")
    if not skills or not effects:
        return []

    known = {row["EffectName"] for row in effects if row["EffectName"]}
    problems = []
    for row in skills:
        if not row["Shape"] or not row["ShapeParams"]:
            continue
        params = parse_shape_params(row["ShapeParams"], row["Shape"],
                                    f"WeaponSkills/{row['Name']}")
        # MORE THAN ONE IS ALLOWED, comma separated, as `Minions` already is.
        # Anathema lays every hex the Wand knows at once and composes from the
        # two that exist rather than inventing a third, so it writes
        # "Shred, Madness". Checked one name at a time: a list validated whole
        # would fail on the comma and say nothing useful about which half is
        # wrong.
        for named in (n.strip() for n in (params.get("Effect") or "").split(",")):
            # A SELF BUFF'S RANDOM BUFF NAMES THE POOL AND NOT A ROW. Issue
            # #2580, [N5]. `check_stalker_kit_keys` already holds it to a
            # SelfBuff and to standing alone.
            if named == RANDOM_BUFF_EFFECT:
                continue
            if named and named not in known:
                problems.append(
                    f"WeaponSkills/{row['Name']}: applies the effect {named!r}, "
                    f"which is not in the Buffs, Debuffs or DoTs sheets")
        problems.extend(refuse_a_pick_among_damage_over_time(
            params, effects, f"WeaponSkills/{row['Name']}"))
    return problems


def refuse_a_pick_among_damage_over_time(params: dict[str, str],
                                         effects: list[dict],
                                         where: str) -> list[str]:
    """A row picking at random among its effects names no damage over time.

    Issue #2580, [N4]. The engine draws among what `NamedEffectTags` keeps, and
    it drops every damage over time `Effect` names. A row picking among
    "Poison, Shred" would therefore always lay the Shred, which is not a pick.
    Asked here rather than in `check_stalker_kit_keys`, because only this
    function holds the Status Effects sheet that says which rows are DoTs.
    """
    if "EffectPick" not in params:
        return []
    over_time = {row["EffectName"] for row in effects
                 if row.get("EffectKind") == "DoT"}
    named = [n.strip() for n in (params.get("Effect") or "").split(",")
             if n.strip()]
    return [f"{where}: picks at random among {named}, and {one!r} is damage "
            f"over time, which Effect never lays. Use Ailment= for it"
            for one in named if one in over_time]


def validate_minion_references(tables: dict[str, list[dict]]) -> list[str]:
    """A skill's `Minions` must name types the minion table defines. Issue #338.

    THE WHOLE POINT OF THE MINION TYPE TABLE is that a creature's numbers live in
    one place, so two skills producing the same creature cannot disagree about
    it. That only holds if the skill names the type; a name with no row is a
    skill that summons something with no stats.

    IT ALSO CHECKS COUNT AGREES. A skill naming one type states how many twice --
    in `Count` and in the `Minions` entry -- and this makes that redundancy a
    guard rather than a chance to drift. Iron Fortress is the reason `Minions`
    carries counts at all: it deploys two ballistae and three spike traps, which
    a single `Count` cannot express, so it states no `Count`.
    """
    skills = tables.get("WeaponSkills")
    types = tables.get("MinionTypes")
    if not skills or not types:
        return []

    known = {row["Name"] for row in types}
    problems = []
    for row in skills:
        if not row["Shape"] or not row["ShapeParams"]:
            continue
        where = f"WeaponSkills/{row['Name']}"
        params = parse_shape_params(row["ShapeParams"], row["Shape"], where)
        written = params.get("Minions")
        if not written and row["Shape"] not in SHAPES_THAT_SUMMON:
            # EVERY OTHER SHAPE NAMES NO MINIONS AND SHOULD NOT. A strike, a
            # projectile, an aura and the rest have nothing to summon.
            continue
        if not written and params.get("Possess") == "1":
            # AND A SUMMON THAT TAKES RATHER THAN MAKES NAMES NONE EITHER.
            # The Staff's Subjugate is a Summon shape that summons nothing:
            # it drives your will into an enemy and keeps the creature that
            # was already there, which has its own stat block. The engine
            # branches on the same flag at `UCataclysmSummonSkill::Possess`.
            continue
        if not written:
            # A SUMMON THAT NAMES NO MINION TYPE IS REFUSED HERE. Issue
            # #1515. It used to be allowed, and the engine gave whatever it
            # produced 30% of the SUMMONER'S weapon damage -- the fallback
            # the owner ruled a bug on 2026-09-17, now deleted. A minion
            # with no type has no damage of its own and would swing for
            # nothing, so the row is a mistake rather than a shape, and it
            # is caught here instead of shipping silently.
            #
            # NO SHIPPED ROW LEAVES IT EMPTY, measured 2026-09-17, so this
            # refuses nothing that exists today.
            problems.append(
                f"{where}: is a {row['Shape']} and names no Minions, so it'd "
                "summon something with no stat block of its own. Name a type "
                "from the Minion Types sheet.")
            continue

        produced = parse_minions(written, where)
        for name in produced:
            if name not in known:
                problems.append(
                    f"{where}: produces {name!r}, which has no row in the "
                    f"Minion Types sheet. It reads {sorted(known)}.")

        count = params.get("Count")
        if count and len(produced) == 1:
            only = next(iter(produced.values()))
            if float(count) != float(only):
                problems.append(
                    f"{where}: Count is {count} and Minions says {only}. One "
                    "of the two is wrong, and nothing would notice at runtime.")
    return problems


def validate_risen_kinds(tables: dict[str, list[dict]]) -> list[str]:
    """A curse's `RisesAs` names a kind exactly one Summon row of its damage type raises. Issue #2885, N5.

    WHAT RISES IS READ FROM THAT ROW: `UCataclysmRisingCurse::MarkOn` finds the
    Summon row of the curse's damage type whose `Minions` names the kind, and
    takes its `Duration`, `FervourReserve`, `Radius`, `Taunts` and `Burn` --
    Death Rattle's husk is Shallow Graves' husk in every figure, which is what
    its sentence says. So the kind must be a Minion Types row, and one such
    Summon row must exist, with a `Duration` (a risen minion's lifetime) and
    not possessing. Two would leave the engine choosing between them by table
    order, so two are refused too. A row with no shape raises nothing, so a
    Summon row still waiting on code -- Grave Mouth, before N12 -- does not
    count, and Soul Snare's wisp waits with it.
    """
    skills = tables.get("WeaponSkills")
    types = tables.get("MinionTypes")
    if not skills or not types:
        return []

    known = {row["Name"] for row in types}
    raisers: dict[tuple[str, str], list[str]] = {}
    for row in skills:
        if row["Shape"] != "Summon" or not row["ShapeParams"]:
            continue
        params = parse_shape_params(row["ShapeParams"], "Summon", f"WeaponSkills/{row['Name']}")
        if is_on(params, "Possess") or float(params.get("Duration", "0")) <= 0.0 or not params.get("Minions"):
            continue
        for kind in parse_minions(params["Minions"], f"WeaponSkills/{row['Name']}"):
            raisers.setdefault((row["DamageType"], kind), []).append(row["Name"])

    problems = []
    for row in skills:
        if not row["Shape"] or not row["ShapeParams"]:
            continue
        where = f"WeaponSkills/{row['Name']}"
        params = parse_shape_params(row["ShapeParams"], row["Shape"], where)
        if params.get("OnDeath") != "Rise":
            continue
        kind = params.get("RisesAs", "").strip()
        if kind not in known:
            problems.append(
                f"{where}: RisesAs={kind!r}, which has no row in the Minion "
                f"Types sheet. It reads {sorted(known)}.")
            continue
        found = raisers.get((row["DamageType"], kind), [])
        if len(found) != 1:
            problems.append(
                f"{where}: RisesAs={kind}, and {len(found)} {row['DamageType']} "
                f"Summon rows with a Duration raise it ({found}); what rises is "
                f"read from exactly one, so one must exist and no second.")
    return problems


def refuse_a_count_cap_on_a_summon(tables: dict[str, list[dict]]) -> list[str]:
    """A Summon row states no `MaxActive` unless it states an `Interval` too.

    ISSUE #2787, BRANCH B, from the owner's ruling of 2026-11-20: nothing that
    reserves the class resource has a count cap ("that's limiting on its
    own"), so the reserve is the only limit on how many of a kind stand.
    Summon Imp's `MaxActive=3` was the one a Summon row stated, and
    `UCataclysmSummonSkill::SummonOne` reads none since. A cap written back
    onto a Summon row would be read by nothing and say something untrue, so it
    is refused here rather than shipped.

    A ROW WITH AN `Interval` IS LEFT ALONE: that is Open the Rift's pattern, a
    spawn every Interval over the skill's Duration, where `SpawnTick` reads
    `MaxActive` as how many one cast makes in all.

    AND A `Deployable` SINCE ISSUE #1934 (2026-11-25): the War machines reserve
    their rows' Fervour and lost their caps, ruled that day under the same
    ruling, and `UCataclysmDeployableSkill::DeployOne` reads no `MaxActive`. A
    Deployable has no Interval pattern, so any `MaxActive` on one is refused.
    """
    problems = []
    for row in tables.get("WeaponSkills") or []:
        shape = row.get("Shape")
        if shape not in ("Summon", "Deployable") or not row.get("ShapeParams"):
            continue
        where = f"WeaponSkills/{row['Name']}"
        params = parse_shape_params(row["ShapeParams"], row["Shape"], where)
        if "MaxActive" in params and (shape == "Deployable" or "Interval" not in params):
            problems.append(
                f"{where}: is a {shape} stating MaxActive={params['MaxActive']}"
                f"{'' if shape == 'Deployable' else ' and no Interval'}. "
                "Nothing that reserves the class resource has a count cap "
                "(issues #2787 and #1934): the reserve is the limit, and "
                "nothing in the game reads this one. Remove it.")
    return problems


#: The two shapes that put a creature of their own in the world. Issue #1515.
#:
#: READ OFF THE DATA RATHER THAN GUESSED: `Summon` is Summon Imp and Open the
#: Rift, `Deployable` is Bolt Turret, Ballista and Iron Fortress, and every
#: other shape in `game/Data/WeaponSkills.csv` names no minions at all. A row
#: of one of these two with no `Minions` parameter is the mistake the check
#: below refuses.
SHAPES_THAT_SUMMON = frozenset({"Summon", "Deployable"})

#: Which shapes find who they hit with the row's `Radius`, and when.
#:
#: READ OFF THE ENGINE RATHER THAN GUESSED. Every entry is a branch in
#: `game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplates.cpp` that hands
#: `ScaledRadiusCm()` to a targeting search:
#:
#: | Shape | Where the radius decides who is found |
#: |---|---|
#: | Strike | `UCataclysmStrikeSkill::SwingOnce`, the cone it swings through |
#: | Projectile | `UCataclysmProjectileSkill::Land`, the line it pierces or the circle it lands in |
#: | Movement | `UCataclysmMovementSkill::ActivateAbility`, the ends of the move or the line along it |
#: | Aura | `UCataclysmAuraSkill::Pulse`, the ring around the caster |
#:
#: THE FOUR SHAPES THAT ARE ABSENT ARE ABSENT ON PURPOSE, and the reason
#: differs for each. `UCataclysmDebuffSkill::ActivateAbility` never reads the
#: radius at all: it searches `Range` around the caster and then sorts what it
#: found by distance to the cursor. `UCataclysmSummonSkill::Possess` makes the
#: same search since issue #1529, so a Summon that takes a creature reads no
#: radius either; it searched a sphere at the aimed point until 2026-10-02,
#: which is why issue #1519 put Summon in this table. A Deployable places
#: machines and searches for nobody. `UCataclysmSelfBuffSkill` guards both of its radius reads with
#: `ScaledRadiusCm() > 0.0f`, so a self buff that states no radius simply has no
#: ring rather than a broken one. Six self buffs, three deployables, three
#: debuffs, one flickering movement and one possessing summon state no radius
#: today -- fourteen rows -- and all fourteen are correct.
#:
#: ONE OF THE FOUR IS CONDITIONAL, which is why this is a function and not a
#: set. `Mode=Flicker` returns from `ActivateAbility` before the switch that
#: reads the radius -- it builds its circuit out of `Range` instead -- so
#: Everywhere at Once is the one Movement row that needs none. Summon Imp's
#: radius is read by `Collapse`, which guards it the way the self buffs do.
def shape_searches_with_the_radius(shape: str, params: dict[str, str]) -> bool:
    """True when a radius of zero would make this row find nobody."""
    if shape in {"Strike", "Projectile", "Aura"}:
        return True
    if shape == "Movement":
        return params.get("Mode") != "Flicker"
    return False


def validate_targeted_shapes_state_a_radius(
        tables: dict[str, list[dict]]) -> list[str]:
    """A row that finds its targets with the radius has to state one.

    ISSUE #1519. Subjugate stated `Range=15` and no `Radius`.
    `FCataclysmSkillShapeParams::RadiusCm` defaults to zero, every branch of
    `UCataclysmSkillTemplate::ScaledRadiusCm` multiplies that zero, and the
    search is a collision overlap, so the Staff's Demonic Ultimate looked for an
    enemy in a sphere of no size and found one only if the aimed point happened
    to land inside a body. It spent its cooldown, dealt no damage and took
    nobody, eleven casts running, and said nothing about why.

    A KEY NOBODY WROTE IS NOT A KEY WRITTEN WRONG, which is the gap this fills.
    `parse_shape_params` already refuses a misspelled key and a value that is
    not a number, and its docstring names this very failure -- "a radius of zero
    hits nothing, so a misspelled `Radiuss` would produce a skill that runs and
    does nothing". A missing key passes all of that and arrives at the engine as
    the default.

    A STATED ZERO IS REFUSED TOO. `Radius=0` produces exactly the same skill as
    no radius at all, so there is nothing to gain by letting a row write it
    down.
    """
    skills = tables.get("WeaponSkills")
    if not skills:
        return []

    problems = []
    for row in skills:
        if not row["Shape"]:
            continue
        where = f"WeaponSkills/{row['Name']}"
        params = parse_shape_params(row["ShapeParams"], row["Shape"], where)
        if not shape_searches_with_the_radius(row["Shape"], params):
            continue

        written = params.get("Radius")
        if written is None:
            problems.append(
                f"{where}: {row['Shape']} finds who it hits with its radius and "
                f"states none, so it would search an area of no size and hit "
                f"nobody. Give it a Radius.")
        elif float(written) <= 0.0:
            problems.append(
                f"{where}: {row['Shape']} states Radius={written}, which finds "
                f"nobody. A radius has to be more than zero.")
    return problems


def validate_affix_percent_agrees(tables: dict[str, list[dict]]) -> list[str]:
    """Two affixes granting one stat must agree on whether it is a percentage.

    The Percent column is filled in per affix row and the fact it records
    belongs to the STAT, so `Flat critical strike chance` and `Increased
    critical strike chance` both name `crit_chance` and both have to say the
    same thing about it. Without this check the column is a second answer that
    can drift from itself, which is the failure this project has already had
    with the power model.
    """
    affix_rows = tables.get("Affixes")
    if not affix_rows:
        return []

    said: dict[str, tuple[str, bool]] = {}
    problems = []
    for row in affix_rows:
        stat = row["Stat"]
        if not stat:
            continue
        if stat in said:
            first, answer = said[stat]
            if answer != row["Percent"]:
                problems.append(
                    f"Affixes/{row['Name']}: says Percent is {row['Percent']} "
                    f"for stat {stat!r}, and Affixes/{first} says "
                    f"{answer}. One stat, one answer.")
        else:
            said[stat] = (row["Name"], row["Percent"])
    return problems


def validate_implicit_stats_have_an_affix(
        tables: dict[str, list[dict]]) -> list[str]:
    """Every stat an item base grants as an implicit must have an affix too.

    The item tool tip reads whether a stat is a percentage off the affix table,
    because that is the only table that records it. An implicit naming a stat
    no affix grants would print with no percent sign and nothing would say so.
    Cross-checking the two sheets keeps that impossible without a second list.
    """
    bases = tables.get("ItemBases")
    affix_rows = tables.get("Affixes")
    if not bases or not affix_rows:
        return []

    covered = {row["Stat"] for row in affix_rows if row["Stat"]}
    problems = []
    for base in bases:
        for field in ("Implicit1Stat", "Implicit2Stat"):
            stat = base.get(field) or ""
            if stat and stat not in covered:
                problems.append(
                    f"ItemBases/{base['Name']}: {field} is {stat!r}, which no "
                    "affix grants, so the tool tip cannot tell whether it is "
                    "a percentage")
    return problems


def validate_hybrid_parts(tables: dict[str, list[dict]]) -> list[str]:
    """A hybrid affix must name two affixes that exist.

    A hybrid grants two stats at a reduced share each. If a part names nothing,
    the hybrid grants half of what it says and the shortfall is invisible.
    """
    affix_rows = tables.get("Affixes")
    if not affix_rows:
        return []

    names = {row["AffixName"] for row in affix_rows}
    problems = []
    for row in affix_rows:
        if row["AffixKind"] != "Hybrid":
            continue
        for field in ("HybridPart1", "HybridPart2"):
            part = row[field]
            if not part:
                problems.append(f"Affixes/{row['Name']}: {field} is empty")
            elif part not in names:
                problems.append(
                    f"Affixes/{row['Name']}: {field} names {part!r}, which is "
                    "not an affix")
    return problems


def validate_stat_names(tables: dict[str, list[dict]]) -> list[str]:
    """Report attribute effects whose stat no class supplies a base for.

    ATTRIBUTES ONLY EVER SCALE. A point adds to a stat's sum of increases and
    the sum multiplies a base, so a point does nothing until something supplies
    that base. The class is not the only thing that can: gear implicits and
    affixes supply block chance, critical strike chance and evasion, and a
    weapon supplies attack speed. So this is information rather than an error.

    It is worth printing because the alternative is nobody noticing. Attack
    speed had no base anywhere at all for some time, which made every attack
    speed affix on every item worth exactly nothing; see issue #120.

    The two sheets are checked against each other rather than against a
    hard-coded list of the 33 stats, so adding a stat to the character sheet
    needs no change here.

    NO STAT IS EXEMPT. Cooldown reduction was, as a "rate" whose class base of
    zero was said to be correct, until issue #2000 found that gear supplies it
    as a flat value and the Efficacy attribute only scales that. It is then
    exactly the case this reports, like block chance and evasion, and issue
    #2004 removed the exemption.
    """
    class_rows = tables.get("ClassStats")
    attribute_rows = tables.get("Attributes")
    if not class_rows or not attribute_rows:
        return []

    from_classes = {row["Stat"] for row in class_rows}
    from_attributes = {row["Stat"] for row in attribute_rows}

    return [f"{stat!r} is scaled by an attribute, and no class supplies its "
            f"base. It does nothing until gear, a weapon or a skill does."
            for stat in sorted(from_attributes - from_classes)]


def validate_element_visuals(tables: dict[str, list[dict]],
                             declared: set[str]) -> list[str]:
    """Every damage type has exactly one effect palette row, and no row invents
    a damage type.

    BOTH DIRECTIONS MATTER AND THE FIRST IS THE QUIET ONE. A damage type with no
    row is a skill whose effects fall back to whatever the Niagara system was
    authored with, so every Void skill would look like whichever damage type the
    artist happened to build the system against. Nothing would report it: the
    table would load, the lookup would miss, and the effect would still play.

    Cross-checked against the Tags sheet rather than against a list written
    here, so adding a ninth damage type to the design needs no change in this
    file -- it fails generation until the palette row exists.
    """
    rows = tables.get("ElementVisuals")
    if not rows:
        return []

    wanted = {tag for tag in declared if tag.startswith(ELEMENT_TAG_PREFIX)}
    have = {row["ElementTag"] for row in rows}

    problems = [
        f"ElementVisuals: {tag} is a declared damage type with no effect "
        f"palette row, so its effects would take whatever colour the Niagara "
        f"system was authored with"
        for tag in sorted(wanted - have)
    ]
    problems += [
        f"ElementVisuals/{row['Name']}: ElementTag {row['ElementTag']!r} is not "
        f"declared on the Tags sheet. The declared damage types are "
        f"{sorted(wanted)}."
        for row in rows if row["ElementTag"] not in wanted
    ]
    return problems


#: The two tables whose ordinary rows may carry a weight of 0, meaning RETIRED:
#: the row stays, with its name and its position, and never drops.
#: `UCataclysmDropRoll::EnchantmentDrawWeight` already prices any weight below 1
#: at nothing. Issue #1833, ruled 2026-09-25: the owner removed three
#: enchantments, and deleting a row would shift the position code of every later
#: row, which merged design-log entries quote. A set row is excluded: its weight is
#: an ordinary 1 since issue #1443, and a set is retired, if ever, as a whole set.
RETIRABLE_WEIGHT_TABLES = ("EnchantmentsPositive", "EnchantmentsNegative")


def is_retired(table: str, row: dict) -> bool:
    """Whether this row is an ordinary enchantment retired with a weight of 0."""
    return (table in RETIRABLE_WEIGHT_TABLES
            and str(row.get("EnchantmentType", "")).casefold() != "set"
            and float(row["Weight"]) == 0)


def validate_weights(tables: dict[str, list[dict]]) -> list[str]:
    problems = []
    for table, rows in tables.items():
        for row in rows:
            if "Weight" in row and is_retired(table, row):
                continue
            if "Weight" in row and not 0 < float(row["Weight"]) <= 100:
                problems.append(f"{table}/{row['Name']}: weight "
                                f"{row['Weight']} is outside 0 to 100")
    return problems


#: The enchantment types the sheet may state.
#:
#: `Set` takes a row out of the ordinary weight bands: since issue #1443
#: `UCataclysmDropRoll::EnchantmentCandidatesFor` leaves out every row of that
#: type, and the set's number is in `SetId`. `Generic` leaves the row in the
#: bands. Nothing else means anything to the game.
ENCHANTMENT_TYPES = ("Generic", "Set")

#: The tables whose rows carry a type. Named rather than found by pattern, so
#: the check below knows what it is meant to have inspected.
ENCHANTMENT_TABLES = ("EnchantmentsPositive", "EnchantmentsNegative")


def validate_enchantment_types(tables: dict[str, list[dict]]) -> list[str]:
    """Every enchantment row states a type the game recognises.

    A BLANK CELL IS THE ONE THAT HIDES, AND ONE WAS BLANK. Issue #1486. A single
    negative row -- "Your maximum HP cannot exceed 40%-60% of its normal value"
    -- carried an empty Type where the other 195 said `Generic` or `Set`, and
    nothing reported it for as long as the sheet existed.

    IT WORKED BY ACCIDENT. The only code that reads the column,
    `UCataclysmDropRoll::EnchantmentSuitsSlot`, asks whether the value IS `Set`
    and refuses the row when it is. An empty string is not `Set`, so the row was
    drawn, which is what the design wants of it. IT BREAKS THE MOMENT ANYTHING
    ASKS THE QUESTION THE OTHER WAY ROUND. A filter that keeps `Generic`, or a
    validator that requires a known type, drops that row instead -- one of the
    eight severe drawbacks written at weight 1, so 12.5% of the weight 1
    drawback pool would disappear with no failing test anywhere.

    A MISSPELLING FAILS TOO, not only a blank, because `Genric` reads as "not a
    set" in exactly the way an empty cell does and is just as quiet.

    THE COMPARISON IGNORES CASE, which is what the game does:
    `EnchantmentSuitsSlot` passes `ESearchCase::IgnoreCase`. A lowercase cell
    works in the game, so failing generation over one would be this file
    inventing a rule the game does not have.

    IT REPORTS INSPECTING NOTHING. A check that looked at no rows passes for the
    same reason a correct sheet does, and from the outside the two are
    identical. If the enchantment tables stop carrying the column this says so
    rather than falling silent.
    """
    known = {name.casefold() for name in ENCHANTMENT_TYPES}
    present = [table for table in ENCHANTMENT_TABLES if table in tables]
    problems = []
    inspected = 0

    for table in present:
        for row in tables[table]:
            if "EnchantmentType" not in row:
                continue
            inspected += 1
            written = str(row["EnchantmentType"]).strip()
            if written.casefold() in known:
                continue
            problems.append(
                f"{table}/{row['Name']}: EnchantmentType is {written!r}. "
                f"Every enchantment row must state one of "
                f"{list(ENCHANTMENT_TYPES)}. A blank or misspelt cell is not "
                f"'Set', so the draw keeps the row by accident and anything "
                f"that filters for 'Generic' instead drops it in silence.")

    if present and not inspected:
        problems.append(
            f"no row of {present} carries an EnchantmentType column, so this "
            f"check inspected nothing and would have passed whatever the "
            f"sheet held. Issue #1486 is the fault it is meant to catch.")

    return problems


def validate_enchantment_set_ids(tables: dict[str, list[dict]]) -> list[str]:
    """A set row carries a set number and a weight of 1 to 4; an ordinary row carries no set number.

    ISSUE #1443, ruled by the project owner on 2026-09-07: "Add a SetId column to the Enchantments sheet and give the
    set rows an ordinary 1-to-4 weight." Until then a set row's Weight held its set number, 5 to 18, so Weight meant
    two things. Each half of the rule is checked, because each mistake is quiet on its own: a set row without a number
    belongs to no set and none of its bonuses can turn on, and an ordinary row with a number would be counted as a set
    piece. A set row's weight must be a whole 1 to 4, because it is now a weight: 0 retires only an ordinary row, and a
    number above 4 is the old meaning come back.
    """
    problems = []
    for table in ENCHANTMENT_TABLES:
        for row in tables.get(table, []):
            is_set = str(row.get("EnchantmentType", "")).strip().casefold() == "set"
            number = str(row.get("SetId", "")).strip()
            number = "" if number in ("", "0") else number
            if is_set and not number:
                problems.append(f"{table}/{row['Name']}: a Set row with no set number. Write the set's number in "
                                f"the sheet's set column (K for a benefit, L for a drawback).")
            if not is_set and number:
                problems.append(f"{table}/{row['Name']}: an {row.get('EnchantmentType')!r} row carries the set "
                                f"number {number}. Only a row typed 'Set' belongs to a set.")
            weight = float(row["Weight"])
            if is_set and (not weight.is_integer() or not 1 <= weight <= HIGHEST_ENCHANTMENT_WEIGHT):
                problems.append(f"{table}/{row['Name']}: a Set row's weight is {row['Weight']}. Since issue #1443 it "
                                f"is an ordinary weight, a whole number from 1 to {HIGHEST_ENCHANTMENT_WEIGHT}; the "
                                f"set's number goes in the set column.")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify without writing; exit 1 if out of date")
    parser.add_argument("--workbook", type=pathlib.Path, default=WORKBOOK)
    parser.add_argument("--output-dir", type=pathlib.Path, default=OUTPUT_DIR)
    args = parser.parse_args(argv)

    import openpyxl

    try:
        book = openpyxl.load_workbook(args.workbook, data_only=True)
        tables = {name: builder(book) for name, builder in TABLES.items()}
        tables.update({name: builder()
                       for name, builder in MODEL_TABLES.items()})
        tables.update({name: builder()
                       for name, builder in TREE_TABLES.items()})
    except DataError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    except KeyError as error:
        print(f"FAIL: the workbook is missing sheet {error}", file=sys.stderr)
        return 1

    # Reported and not fatal. An attribute scaling a stat no class supplies a
    # base for is the design's own stated case: attributes only ever scale, so
    # a point does nothing until gear, a weapon or a skill supplies the base.
    # Worth printing, because the alternative is nobody noticing.
    notes = validate_stat_names(tables)
    if notes:
        print(f"NOTE: {len(notes)} attribute effect(s) scale a stat no class "
              f"supplies:", file=sys.stderr)
        for line in notes:
            print(f"  {line}", file=sys.stderr)

    problems = (validate_tags(tables, known_tags(book))
                + validate_weights(tables)
                + validate_enchantment_types(tables)
                + validate_enchantment_set_ids(tables)
                + validate_affix_slots(tables)
                + validate_socket_slots(tables)
                + validate_enemy_drop_rarities(tables)
                + validate_material_tier_counts(tables)
                + validate_upgrade_stone_levels(tables)
                + validate_weapon_skill_types(tables)
                + validate_weapon_skill_damage_types(tables, declared_tags(book))
                + validate_weapon_tags(tables, declared_tags(book))
                + validate_skill_effects(tables)
                + validate_minion_references(tables)
                + validate_risen_kinds(tables)
                + refuse_a_count_cap_on_a_summon(tables)
                + validate_targeted_shapes_state_a_radius(tables)
                + validate_hybrid_parts(tables)
                + validate_affix_percent_agrees(tables)
                + validate_implicit_stats_have_an_affix(tables)
                + validate_element_visuals(tables, declared_tags(book))
                + validate_passive_effects(tables, known_tags(book))
                + validate_enchantment_effects(tables, known_tags(book)))
    if problems:
        print(f"FAIL: {len(problems)} validation problem(s):", file=sys.stderr)
        for line in problems[:40]:
            print(f"  {line}", file=sys.stderr)
        if len(problems) > 40:
            print(f"  ... and {len(problems) - 40} more", file=sys.stderr)
        return 1

    stale = []
    for name, rows in tables.items():
        contents = render_csv(rows)
        path = args.output_dir / f"{name}.csv"
        if args.check:
            if not path.is_file() or path.read_text(encoding="utf-8") != contents:
                stale.append(path.name)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")

    if args.check:
        if stale:
            print(f"FAIL: {len(stale)} DataTable CSV(s) out of date. "
                  "Run without --check.", file=sys.stderr)
            for name in sorted(stale):
                source = ("sim/cataclysm_sim/enemy_stats.py"
                          if pathlib.Path(name).stem in MODEL_TABLES
                          else args.workbook.name)
                print(f"  {name:<28}behind {source}", file=sys.stderr)
            return 1
        print(f"All {len(tables)} DataTable CSVs are up to date.")
        return 0

    for name, rows in sorted(tables.items()):
        print(f"  {name + '.csv':<28}{len(rows):>5} rows")
    # THE DIRECTORY IT ACTUALLY WROTE TO, NOT `game/Data/`. Issue #1487. This
    # line named `game/Data/` whatever `--output-dir` was, because the option
    # was added after the message. The safe way to see what a workbook edit does
    # is to generate into a scratch directory and compare, and this line told
    # you that had written over the repository's data instead.
    print(f"Wrote {len(tables)} CSVs to {args.output_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
