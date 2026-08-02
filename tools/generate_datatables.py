"""Turn the design workbook into DataTable CSVs for Unreal.

    python tools/generate_datatables.py            # write the CSVs
    python tools/generate_datatables.py --check    # verify, change nothing

Output goes to game/Data/. Each CSV matches a USTRUCT declared in
game/Source/Cataclysm/Data/CataclysmDataRows.h, and an Unreal automation test
loads every CSV through its struct so a mismatch fails the build rather than
being discovered when something silently reads nothing.

WHY THIS IS NOT A LOOP OVER SHEETS

Only four of the nine sheets are a plain table with one entity per row. The rest
need reshaping, so each has its own handler:

  Enchantments      two independent tables side by side in one sheet, positives
                    in columns A-D and negatives in F-I
  Enemy Modifiers   a matrix, one column per Cataclysm, each cell holding
                    "Name: Description"
  Buffs, Debuffs    single column, no header row, each cell "Name: Description"
  DoTs              same shape as Buffs

Row names are derived and must be stable, because a DataTable row name is the key
everything else references. Renaming one silently breaks every reference to it.
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

    problems = (validate_tags(tables, known_tags(book))
                + validate_weights(tables))
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
