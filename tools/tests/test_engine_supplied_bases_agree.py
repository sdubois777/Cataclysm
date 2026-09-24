"""The stats the engine supplies a base for are one list, though written twice.

WHY THIS EXISTS. Issue #1674. The list is written in two places:

| Where | What it does | A stat missing from it |
|---|---|---|
| `ENGINE_SUPPLIED_BASES` in `tools/generate_datatables.py` | exempts the stat from "an increase with no base under it" | generating the data fails, loudly |
| `UCataclysmPlayerClassStats::EngineSuppliedBases()` in `CataclysmPlayerClassStats.cpp` | puts the base on a real character | nothing reports it, and the stat is zero for every player |

So a stat in the Python list and not in the C++ map generates cleanly and does
nothing in play. That happened twice before this check existed: issue #1025
(`damage_to_bleeding_window`, a conversion window of zero seconds) and the
stagger work for issue #45 (`stagger_duration`, no player could stagger
anything). The two existing guards each read one side only:
`test_every_engine_supplied_base_names_code_that_exists` walks the Python list,
and `Cataclysm.PlayerStats.EveryEngineSuppliedBaseReachesACharacter` walks the
entries the C++ map holds, so neither sees a stat that is on one side alone.

HOW THE C++ MAP IS READ, because it is not a list of strings.

- **Written entries** are pairs such as
  `{FName(UCataclysmDebuffs::DurationStat), UCataclysmDebuffs::NormalDuration}`.
  The key is a named constant, and each is resolved to its string from its
  definition, `const TCHAR* UCataclysmDebuffs::DurationStat = TEXT("...")`.
- **The ailment magnitudes are added by a loop** over
  `UCataclysmAilments::Kinds()`, one for each kind whose `MagnitudeStat` is set.
  Those names are read from the kinds table in `CataclysmAilments.cpp`, field by
  field.

COMMENTS ARE STRIPPED FIRST, by a stripper that knows about string literals, so
a commented-out entry is not an entry and a `//` inside a `TEXT("...")` does not
cut a line short. A constant that cannot be resolved FAILS BY NAME rather than
being dropped, which would make the two sets differ for the wrong reason or,
worse, agree for one.
"""

from __future__ import annotations

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import generate_datatables as gen  # noqa: E402

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source"
PLAYER_CLASS_STATS = SOURCE / "Cataclysm" / "Character" / "CataclysmPlayerClassStats.cpp"
AILMENTS = SOURCE / "Cataclysm" / "AbilitySystem" / "CataclysmAilments.cpp"

#: The field of an `FCataclysmAilmentKind` initialiser that holds `MagnitudeStat`,
#: counting from zero, and how many fields an initialiser has. Read off the
#: struct in `CataclysmAilments.h`; `kinds_table_magnitudes` refuses an entry of
#: any other length, so a field added to the struct fails here by name.
MAGNITUDE_FIELD = 6
KIND_FIELDS = 9

WRITTEN_ENTRY = re.compile(
    r"\{\s*FName\(\s*(\w+::\w+)\s*\)\s*,\s*(\w+::\w+)\s*\}")
LOOP_ADD = re.compile(
    r"Built\.Add\(\s*FName\(\s*Kind\.MagnitudeStat\s*\)\s*,\s*(\w+::\w+)\s*\)")


def strip_comments(text: str) -> str:
    """C++ text with `//` and `/* */` comments removed and strings kept whole.

    Line breaks inside a removed block comment are kept, so nothing downstream
    that counts lines is moved.
    """
    out: list[str] = []
    index, length = 0, len(text)
    while index < length:
        if text.startswith("//", index):
            end = text.find("\n", index)
            index = length if end < 0 else end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            stop = length if end < 0 else end + 2
            out.append("\n" * text.count("\n", index, stop))
            index = stop
        elif text[index] in "\"'":
            quote, end = text[index], index + 1
            while end < length and text[end] != quote:
                end += 2 if text[end] == "\\" else 1
            out.append(text[index:end + 1])
            index = end + 1
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def braced_block(text: str, opening: int) -> str:
    """The text between the brace at `opening` and the brace that closes it."""
    assert text[opening] == "{", text[opening:opening + 40]
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError("an opening brace with no closing brace")


def top_level_parts(block: str) -> list[str]:
    """`block` split at the commas that are not inside braces or brackets."""
    parts, depth, start = [], 0, 0
    for index, char in enumerate(block):
        if char in "({[":
            depth += 1
        elif char in ")}]":
            depth -= 1
        elif char == "," and depth == 0:
            parts.append(block[start:index].strip())
            start = index + 1
    tail = block[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def engine_supplied_bases_body(stripped: str) -> str:
    """The body of `UCataclysmPlayerClassStats::EngineSuppliedBases()`."""
    found = re.search(r"UCataclysmPlayerClassStats::EngineSuppliedBases\(\)\s*(\{)",
                      stripped)
    assert found, (
        "no definition of UCataclysmPlayerClassStats::EngineSuppliedBases() in "
        f"{PLAYER_CLASS_STATS.name}, so this check has nothing to read")
    return braced_block(stripped, found.start(1))


def written_entries(body: str) -> list[tuple[str, str]]:
    """(key constant, value symbol) for every pair written out in the map."""
    return WRITTEN_ENTRY.findall(body)


def loop_value_symbol(body: str) -> str | None:
    """The value the ailment loop adds, or None when there is no such loop."""
    if "UCataclysmAilments::Kinds()" not in body:
        return None
    found = LOOP_ADD.search(body)
    assert found, (
        "EngineSuppliedBases() loops over UCataclysmAilments::Kinds() but no "
        "`Built.Add(FName(Kind.MagnitudeStat), ...)` was read inside it, so the "
        "stats that loop adds cannot be named here. Update LOOP_ADD.")
    return found.group(1)


def kinds_table_magnitudes(stripped_ailments: str) -> tuple[int, list[str]]:
    """How many kinds the table has, and every `MagnitudeStat` it sets."""
    found = re.search(r"EveryKind\[\]\s*=\s*(\{)", stripped_ailments)
    assert found, f"no `EveryKind[] = {{` table in {AILMENTS.name}"
    kinds = [part for part in top_level_parts(braced_block(
        stripped_ailments, found.start(1)))]
    magnitudes = []
    for kind in kinds:
        assert kind.startswith("{") and kind.endswith("}"), kind[:80]
        fields = top_level_parts(kind[1:-1])
        assert len(fields) == KIND_FIELDS, (
            f"an ailment kind has {len(fields)} fields where {KIND_FIELDS} were "
            f"expected, so field {MAGNITUDE_FIELD} may not be MagnitudeStat any "
            f"more. Read FCataclysmAilmentKind in CataclysmAilments.h and update "
            f"MAGNITUDE_FIELD and KIND_FIELDS. The kind: {fields[:1]}")
        field = fields[MAGNITUDE_FIELD]
        if field == "nullptr":
            continue
        named = re.fullmatch(r'TEXT\(\s*"([^"]+)"\s*\)', field)
        assert named, (
            f"MagnitudeStat of the kind {fields[0]} is {field!r}, which is neither "
            f"nullptr nor TEXT(\"...\")")
        magnitudes.append(named.group(1))
    return len(kinds), magnitudes


def constant_strings(stripped_sources: str) -> dict[str, str]:
    """Every `const TCHAR* Class::Name = TEXT("...")` in the given source."""
    return dict(re.findall(
        r'const\s+TCHAR\s*\*\s*(\w+::\w+)\s*=\s*TEXT\(\s*"([^"]*)"\s*\)',
        stripped_sources))


def engine_side(player_class_stats: str, ailments: str,
                every_source: str) -> tuple[dict[str, str], list[str], dict]:
    """The C++ map as {stat: value symbol}, plus what could not be resolved.

    Returns the map, the constants that did not resolve, and counts of how many
    entries each reading path produced, for the controls.
    """
    body = engine_supplied_bases_body(strip_comments(player_class_stats))
    strings = constant_strings(strip_comments(every_source))

    stats: dict[str, str] = {}
    unresolved: list[str] = []
    written = written_entries(body)
    for key, value in written:
        if key in strings:
            stats[strings[key]] = value
        else:
            unresolved.append(key)

    looped = 0
    kinds = 0
    value = loop_value_symbol(body)
    if value is not None:
        kinds, magnitudes = kinds_table_magnitudes(strip_comments(ailments))
        for stat in magnitudes:
            stats[stat] = value
        looped = len(magnitudes)

    return stats, unresolved, {"written": len(written), "looped": looped,
                               "kinds": kinds}


def read_engine_side():
    every_source = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in sorted(SOURCE.rglob("*.cpp")))
    return engine_side(
        PLAYER_CLASS_STATS.read_text(encoding="utf-8", errors="replace"),
        AILMENTS.read_text(encoding="utf-8", errors="replace"),
        every_source)


def test_the_engine_side_is_read_whole():
    """The controls. A reader that found nothing would make the check below
    compare an empty set, and one that dropped an entry would compare the
    wrong one.

    Measured 2026-09-23 on `development` at 47a9a6c7: five written entries,
    eleven ailment kinds, two of them with a magnitude stat.
    """
    stats, unresolved, counts = read_engine_side()
    assert not unresolved, (
        "these key constants in EngineSuppliedBases() could not be resolved to "
        "a string, so their stats cannot be compared: "
        + ", ".join(unresolved)
        + ". Each needs a definition of the form "
          "`const TCHAR* Class::Name = TEXT(\"...\")` under game/Source/.")
    assert counts["written"] >= 5, counts
    assert counts["kinds"] >= 10, counts
    assert counts["looped"] >= 2, counts
    assert len(stats) == counts["written"] + counts["looped"], (
        "two entries resolved to the same stat name", stats, counts)


def test_the_python_and_engine_lists_name_the_same_stats():
    """The check itself.

    WHAT TO DO WHEN THIS FAILS. A stat only in the Python list is zero for
    every player until the C++ map gives it a base: add it there, with the
    constant that states its normal value. A stat only in the C++ map is
    refused by the generator only if a row increases it; add it to the Python
    list with the symbol that supplies it.
    """
    stats, unresolved, _ = read_engine_side()
    assert not unresolved, unresolved
    python_side = set(gen.ENGINE_SUPPLIED_BASES)
    engine = set(stats)
    only_python = sorted(python_side - engine)
    only_engine = sorted(engine - python_side)
    assert not only_python and not only_engine, (
        f"\n  only in ENGINE_SUPPLIED_BASES (zero for every player): {only_python}"
        f"\n  only in EngineSuppliedBases() (the generator does not know): "
        f"{only_engine}")


def test_each_python_entry_names_the_value_the_engine_uses():
    """Each Python entry says which constant supplies the base. That constant
    has to be the one the C++ entry for the same stat uses, or the entry is a
    description of some other code."""
    stats, unresolved, _ = read_engine_side()
    assert not unresolved, unresolved
    wrong = {
        stat: (supplier, stats[stat])
        for stat, supplier in gen.ENGINE_SUPPLIED_BASES.items()
        if stat in stats and stats[stat] not in supplier
    }
    assert not wrong, (
        "these ENGINE_SUPPLIED_BASES entries name a supplier the C++ map does "
        "not use for that stat, as (Python text, C++ value): " + repr(wrong))


# ---------------------------------------------------------------------------
# The reader, on hand-made C++, so each rule is shown to act.
# ---------------------------------------------------------------------------

HAND_MADE_MAP = """
const TMap<FName, float>& UCataclysmPlayerClassStats::EngineSuppliedBases()
{
	static const TMap<FName, float> Map = []
	{
		TMap<FName, float> Built = {
			{FName(UA::WindowStat), UA::BaseWindow},
			// {FName(UA::CommentedStat), UA::BaseWindow},
			/* {FName(UA::BlockedStat), UA::BaseWindow}, */
			{FName(UA::MissingStat), UA::BaseWindow},
		};
		for (const FCataclysmAilmentKind& Kind : UCataclysmAilments::Kinds())
		{
			if (Kind.MagnitudeStat)
			{
				Built.Add(FName(Kind.MagnitudeStat), UA::NormalMagnitude);
			}
		}
		return Built;
	}();
	return Map;
}
"""

HAND_MADE_AILMENTS = """
	const FCataclysmAilmentKind EveryKind[] = {
		{TEXT("Bleed"), TEXT("bleed_chance"), TEXT("a"), TEXT("b"), TEXT("c"),
		 &Get, nullptr, nullptr, EShape::DamageOverTime},
		{TEXT("Cripple"), TEXT("cripple_chance"), TEXT("a"), TEXT("b"), TEXT("c // d"),
		 &Get, TEXT("cripple_magnitude"), &GetMagnitude, EShape::Stronger},
	};
"""

HAND_MADE_SOURCES = """
const TCHAR* UA::WindowStat = TEXT("window");
const TCHAR* UA::CommentedStat = TEXT("commented");
const TCHAR* UA::BlockedStat = TEXT("blocked");
"""


def test_a_commented_out_entry_is_not_an_entry_and_an_unresolved_one_is_named():
    stats, unresolved, counts = engine_side(
        HAND_MADE_MAP, HAND_MADE_AILMENTS, HAND_MADE_SOURCES)
    assert stats == {"window": "UA::BaseWindow",
                     "cripple_magnitude": "UA::NormalMagnitude"}, stats
    assert unresolved == ["UA::MissingStat"], unresolved
    assert counts == {"written": 2, "looped": 1, "kinds": 2}, counts
