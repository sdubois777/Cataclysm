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
import io
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
    "FinalHitPercent": "percent of weapon damage a closing hit deals, if any",
    "HealthCostPercent": "percent of current health one use costs, if any",
    "Effect": "the named status effect this applies, from the Buffs, Debuffs or "
              "DoTs sheet",
}

#: The parameters whose value is a name rather than a number.
TEXT_PARAMS = frozenset({"Mode", "Effect"})

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
    "Strike": {"Radius", "Angle", "MaxTargets", "Duration", "Interval", "Knockback"},
    "Projectile": {"Range", "Radius", "Pierce", "Returns", "Speed", "Arc"},
    "SelfBuff": {"Duration", "Radius", "IncreasePerBurning"},
    "Movement": {"Mode", "Range", "Radius"},
    "Summon": {"Range", "Radius", "Count", "MaxActive", "Duration", "Interval"},
    "Aura": {"Radius", "Duration", "Interval"},
    "Debuff": {"Range", "Radius", "MaxTargets", "Duration"},
}

#: The only non-numeric parameter, and the values it may take.
MOVEMENT_MODES = {"Leap", "Charge", "Blink"}


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

    return params


def weapon_skills(book) -> list[dict]:
    out = []
    for index, raw in enumerate(book["Weapon Skills"].iter_rows(values_only=True), 1):
        if index == 1 or not raw or not clean(raw[0]):
            continue
        weapon, damage, slot = clean(raw[0]), clean(raw[1]), clean(raw[2])
        name = clean(raw[3])
        shape = clean(raw[6]) if len(raw) > 6 else ""
        params = clean(raw[7]) if len(raw) > 7 else ""
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
            parse_shape_params(params, shape, where)

        out.append({"Name": row_name(damage, weapon, slot),
                    "WeaponType": weapon, "DamageType": damage, "Slot": slot,
                    "SkillName": name,
                    "SkillDescription": clean(raw[4]),
                    "Tags": tags_with_slot(clean(raw[5]), slot, where),
                    "Shape": shape,
                    "ShapeParams": params})
    return unique(out, "Weapon Skills")


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
    out = []
    for index, raw in enumerate(book["Enchantments"].iter_rows(values_only=True), 1):
        if index == 1 or not raw or len(raw) <= first + 3:
            continue
        text = clean(raw[first])
        if not text:
            continue
        kind = "Negative" if negative else "Positive"
        out.append({"Name": row_name(kind, text[:48]),
                    "Effect": text,
                    "EnchantmentType": clean(raw[first + 1]),
                    "Weight": number(raw[first + 2], "Weight", index),
                    "Tags": clean(raw[first + 3]),
                    "IsNegative": "True" if negative else "False"})
    return unique(out, f"Enchantments ({'negative' if negative else 'positive'})")


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


def status_effects(book) -> list[dict]:
    """Buffs, Debuffs and DoTs: "Name: Description", then two optional numbers.

    The first row is data, not a header. Reading it as a header would silently
    drop one effect from each of the three sheets.

    COLUMNS B AND C ARE HOW LONG AN EFFECT LASTS AND WHAT IT IS WORTH, and both
    are optional because almost every row states neither. They were added for
    Burn, which every one of the sixteen designed Demonic skills applies and
    which stated no duration and no damage anywhere in the design -- so a skill
    reading "sets each one alight" applied an effect with no numbers in it.
    A row that leaves them empty reads as zero and applies nothing, which is
    the honest answer for an effect nobody has designed yet.
    """
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
            duration = raw[1] if len(raw) > 1 else None
            share = raw[2] if len(raw) > 2 else None
            out.append({"Name": row_name(kind, name), "EffectKind": kind,
                        "EffectName": name, "Description": description,
                        "DurationSeconds": number(duration, "DurationSeconds", index)
                            if duration is not None else 0.0,
                        "PercentOfHit": number(share, "PercentOfHit", index)
                            if share is not None else 0.0})
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


def city_upgrades(book) -> list[dict]:
    """City upgrades and their tier 2 and tier 3 scaling.

    A trailing asterisk on the branch name marks a ONE-TIME USE upgrade: it fires
    once and is spent, rather than being a standing improvement. The asterisk is
    turned into a flag here and stripped from the branch name, so nothing has to
    parse punctuation to know how an upgrade behaves.

    One row has no branch. It is also one-time use, has no tiers, and is a last
    resort rather than a city improvement. Which branch it belongs to has not
    been decided, so Branch is left empty and BranchUndecided marks it.
    """
    out = []
    for index, raw in enumerate(book["City Upgrades"].iter_rows(values_only=True), 1):
        if index == 1 or not raw:
            continue
        branch, effect = clean(raw[0]), clean(raw[1])
        if not effect:
            continue

        one_time = branch.endswith("*") or not branch
        branch = branch.rstrip("*").strip()

        t2_kind, t2_value, t2_interval = parse_tier(
            raw[2] if len(raw) > 2 else "", effect, "Tier 2", index)
        t3_kind, t3_value, t3_interval = parse_tier(
            raw[3] if len(raw) > 3 else "", effect, "Tier 3", index)

        out.append({"Name": row_name(branch or "Unbranched", effect[:40]),
                    "Branch": branch,
                    "BranchUndecided": "True" if not branch else "False",
                    "IsOneTimeUse": "True" if one_time else "False",
                    "Effect": effect,
                    # The raw cell is kept alongside the parsed values so nothing
                    # is lost if the inference above is ever wrong.
                    "Tier2Raw": clean(raw[2]) if len(raw) > 2 else "",
                    "Tier2Kind": t2_kind,
                    "Tier2Value": t2_value,
                    "Tier2IntervalDays": t2_interval,
                    "Tier3Raw": clean(raw[3]) if len(raw) > 3 else "",
                    "Tier3Kind": t3_kind,
                    "Tier3Value": t3_value,
                    "Tier3IntervalDays": t3_interval})
    return unique(out, "City Upgrades")


def crafting_materials(book) -> list[dict]:
    out = []
    for index, raw in enumerate(book["Crafting"].iter_rows(values_only=True), 1):
        if index == 1 or not raw or not clean(raw[0]):
            continue
        name = clean(raw[0])
        out.append({"Name": row_name("Material", name), "MaterialName": name,
                    "TierAndSource": clean(raw[1]),
                    "PrimaryUse": clean(raw[2]),
                    "Functions": clean(raw[3]),
                    "CRMetric": clean(raw[6]) if len(raw) > 6 else "",
                    "Formula": clean(raw[7]) if len(raw) > 7 else "",
                    "Outcome": clean(raw[8]) if len(raw) > 8 else ""})
    return unique(out, "Crafting")


IMPLICIT_KINDS = ("flat", "increased")
AFFIX_KINDS = ("Stat", "Resistance", "Ailment", "Hybrid")
AFFIX_POSITIONS = ("prefix", "suffix")


def _header_index(rows: list, sheet: str) -> dict[str, int]:
    """Map header text to column index, so a reordered sheet still reads."""
    if not rows:
        raise DataError(f"{sheet} is empty")
    headers = {clean(h): i for i, h in enumerate(rows[0]) if clean(h)}
    if not headers:
        raise DataError(f"{sheet} has no header row")
    return headers


def _cell(raw, headers: dict[str, int], name: str) -> str:
    index = headers.get(name)
    if index is None or index >= len(raw):
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
        }

        implicits = 0
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
            entry[f"Implicit{slot_index}Stat"] = stat
            entry[f"Implicit{slot_index}Kind"] = kind
            entry[f"Implicit{slot_index}Value"] = float(value) if value else 0.0

        if implicits == 0:
            raise DataError(f"Item Bases row {index}: {name} grants nothing, so "
                            "it is not a distinct base")
        out.append(entry)

    return unique(out, "Item Bases")


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
        })

    return unique(out, "Affixes")


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
    `docs/Cataclysm_GDD_v2.md` line 1588: "Every minion type has its own stats. A
    minion is not a percentage of its summoner." Each is the type's own base
    raised by the summoner's level, in the same `Base` and `Per Level` shape
    `Class Stats` already uses for character stats.

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

        out.append({
            "Name": name,
            "Family": family,
            "BaseHealth": health,
            "HealthPerLevel": number(_cell(raw, headers, "Health Per Level"),
                                     "Health Per Level", index),
            "BaseDamage": damage,
            "DamagePerLevel": number(_cell(raw, headers, "Damage Per Level"),
                                     "Damage Per Level", index),
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

    IMPORTING IT RUNS ITS OWN CHECKS. enemy_stats.py calls four self-checks at
    module scope: every archetype deals a real damage type, none can reach the
    70% resistance the design caps at, every body has a width, and every
    creature can turn. So a model that contradicts itself raises here rather
    than being written out to a CSV.
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
        })

    if not out:
        raise DataError("enemy_stats.RARITY_ORDER is empty")
    return unique(out, "Enemy Rarities")


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
    "ItemBases": item_bases,
    "Affixes": affixes,
    "ClassStats": class_stats,
    "MinionTypes": minion_types,
    "MinionScaling": minion_scaling,
    "Attributes": attributes,
    "SkillSlots": skill_slots,
    "ElementVisuals": element_visuals,
}

#: Tables built from sim/cataclysm_sim/enemy_stats.py rather than the workbook.
#: Kept apart from TABLES only so a stale-file message can name the right source;
#: everything downstream treats the two the same.
MODEL_TABLES = {
    "EnemyArchetypes": enemy_archetypes,
    "EnemyRarities": enemy_rarities,
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
        named = params.get("Effect")
        if named and named not in known:
            problems.append(
                f"WeaponSkills/{row['Name']}: applies the effect {named!r}, "
                f"which is not in the Buffs, Debuffs or DoTs sheets")
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


#: Stats whose base is not a quantity a class holds.
#:
#: Cooldown reduction is the accumulated sum of increases rather than a value:
#: a skill's cooldown is its own base divided by one plus this. A class base of
#: zero is correct for it, so it is not worth reporting.
RATE_STATS = frozenset({"cooldown_reduction"})


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
    """
    class_rows = tables.get("ClassStats")
    attribute_rows = tables.get("Attributes")
    if not class_rows or not attribute_rows:
        return []

    from_classes = {row["Stat"] for row in class_rows}
    from_attributes = {row["Stat"] for row in attribute_rows}

    return [f"{stat!r} is scaled by an attribute, and no class supplies its "
            f"base. It does nothing until gear, a weapon or a skill does."
            for stat in sorted(from_attributes - from_classes - RATE_STATS)]


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


def validate_weights(tables: dict[str, list[dict]]) -> list[str]:
    problems = []
    for table, rows in tables.items():
        for row in rows:
            if "Weight" in row and not 0 < float(row["Weight"]) <= 100:
                problems.append(f"{table}/{row['Name']}: weight "
                                f"{row['Weight']} is outside 0 to 100")
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
                + validate_affix_slots(tables)
                + validate_weapon_skill_types(tables)
                + validate_skill_effects(tables)
                + validate_hybrid_parts(tables)
                + validate_element_visuals(tables, declared_tags(book)))
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
    print(f"Wrote {len(tables)} CSVs to game/Data/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
