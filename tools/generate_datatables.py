"""Turn the design workbook into DataTable CSVs for Unreal.

    python tools/generate_datatables.py            # write the CSVs
    python tools/generate_datatables.py --check    # verify, change nothing

Output goes to game/Data/. Each CSV matches a USTRUCT declared in
game/Source/Cataclysm/Data/CataclysmDataRows.h, and an Unreal automation test
loads every CSV through its struct so a mismatch fails the build rather than
being discovered when something silently reads nothing.

WHY THIS IS NOT A LOOP OVER SHEETS

Only six of the eleven tables come from a sheet that is already a plain table
with one entity per row. The rest need reshaping, so each has its own handler:

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

CATACLYSM_TYPES = ["Demonic", "Death", "War", "Pestilence", "Famine",
                   "Celestial", "Chaos", "Void", "Generic"]


class DataError(Exception):
    """A problem in the workbook that must stop generation."""


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


def weapon_skills(book) -> list[dict]:
    out = []
    for index, raw in enumerate(book["Weapon Skills"].iter_rows(values_only=True), 1):
        if index == 1 or not raw or not clean(raw[0]):
            continue
        weapon, damage, slot = clean(raw[0]), clean(raw[1]), clean(raw[2])
        out.append({"Name": row_name(damage, weapon, slot),
                    "WeaponType": weapon, "DamageType": damage, "Slot": slot,
                    "SkillName": clean(raw[3]),
                    "SkillDescription": clean(raw[4]),
                    "Tags": clean(raw[5])})
    return unique(out, "Weapon Skills")


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
    """Buffs, Debuffs and DoTs: one column, no header, "Name: Description".

    The first row is data, not a header. Reading it as a header would silently
    drop one effect from each of the three sheets.
    """
    out = []
    for sheet, kind in (("Buffs", "Buff"), ("Debuffs", "Debuff"), ("DoTs", "DoT")):
        for raw in book[sheet].iter_rows(values_only=True):
            if not raw:
                continue
            text = clean(raw[0])
            if not text:
                continue
            name, description = split_named(text)
            if not name:
                name = text[:40]
            out.append({"Name": row_name(kind, name), "EffectKind": kind,
                        "EffectName": name, "Description": description})
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
    for index, raw in enumerate(rows[1:], start=2):
        if not raw or not clean(raw[0]):
            continue
        name, effect = clean(raw[0]), clean(raw[1])

        match = re.search(r"(\d+(?:\.\d+)?)\s*%", effect)
        if not match:
            raise DataError(f"Gems row {index}: {name!r} states no percentage in "
                            f"its effect text, so the Everyday value cannot be "
                            f"read: {effect!r}")
        everyday = float(match.group(1)) / 100.0

        entry = {"Name": row_name("Gem", name), "GemName": name,
                 "Effect": effect,
                 "GemType": clean(raw[9]) if len(raw) > 9 else "",
                 "Everyday": everyday}
        for offset, tier in enumerate(tiers, start=2):
            entry[tier] = number(raw[offset], tier, index) if offset < len(raw) else 0.0
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
            "DamageTypeSlots": int(float(_cell(raw, headers, "Damage Types") or 0)),
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
    "Attributes": attributes,
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


def known_tags(book) -> set[str]:
    """Every declared tag plus the parents Unreal creates implicitly."""
    declared = {clean(r[0]) for r in book["Tags"].iter_rows(values_only=True)
                if r and clean(r[0])} - {"Tag Name"}
    known: set[str] = set()
    for tag in declared:
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
                + validate_hybrid_parts(tables))
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
            print(f"FAIL: out of date with {args.workbook.name}: "
                  f"{', '.join(sorted(stale))}. Run without --check.",
                  file=sys.stderr)
            return 1
        print(f"All {len(tables)} DataTable CSVs are up to date.")
        return 0

    for name, rows in sorted(tables.items()):
        print(f"  {name + '.csv':<28}{len(rows):>5} rows")
    print(f"Wrote {len(tables)} CSVs to game/Data/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
