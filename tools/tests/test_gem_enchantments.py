"""The two enchantments that wait on gems: issue #46, slice G7, ruled by the coordinating session on 2026-11-22.

"Sockets on this equipment fit any gem type" and "Gems in this equipment are 50% less effective" are read off the PIECE
that carries them, by `UCataclysmGems::FitsAnyGem` and `UCataclysmGems::SocketedGemEffectOf`, and by no character, as
`damage_types_carried` is read off a weapon. The rulings: fit-any is read at socket time and at apply time and never
written into a socket as Any (Q1); a gem it let in stays socketed and stops acting when the piece no longer carries it
(Q2, a judgement); the gem effect is local to the piece and multiplies each gem's value before its bucket (Q3, derived
from Path of Exile's Darkness Enthroned); and it is not rounded (Q4).

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  1. the generator and the engine name the two stats alike, and both are on `StatsWithNoAttribute` -- a stat off that
     list is refused by the generator and dropped by `ApplyTo`, and a misspelt one is read by nothing;
  2. the generator refuses every row shape the gem code would read wrongly, in its function and in the whole sheet's
     validation;
  3. the two real sentences' rows carry through the generator unrefused, on a made-up sheet (the real rows are the
     workbook stage's);
  4. fit-any is asked at both moments -- the Forge's socket check, the Forge screen's list and the gems' modifiers --
     and nothing writes Any into a socket (Q1, Q2);
  5. the gem effect multiplies each gem's value through the one call that adds it, row by row, never below nought (Q3).
"""
from __future__ import annotations

import pathlib
import re
import sys

import openpyxl
import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))
import generate_datatables as gen  # noqa: E402

SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
GEMS_CPP = SOURCE / "Items" / "CataclysmGems.cpp"
ITEM_CPP = SOURCE / "Items" / "CataclysmItem.cpp"
FORGE_CPP = SOURCE / "Items" / "CataclysmForge.cpp"
LAYOUT_CPP = SOURCE / "Interface" / "CataclysmForgeLayout.cpp"

FITS_ANY = "Sockets on this equipment fit any gem type"
LESS_EFFECTIVE = "Gems in this equipment are 50% less effective"

#: The Enchantment Effects sheet's header, as the workbook held it on 2026-11-22.
EFFECTS_HEADER = ["Enchantment", "Effect", "Stat", "Value Kind", "Value Low", "Value High", "Required Tags",
                  "Condition", "Condition Value", "Scale", "Scale Step", "Action", "Action Event", "Fraction Of",
                  "Scale Max Steps", "Scale Offset", "Stack Seconds", "Every Seconds", "Every Nth", "Scale Step High",
                  "Stack Seconds High", "Condition 2", "Condition Value 2", "Condition Value High", "Trigger Cooldown",
                  "Event Value", "Ailment", "Damage Share"]


def code(path: pathlib.Path) -> str:
    """The file with its comments removed, so a name written only in a comment does not count."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    """The function opening at `signature`, to its closing brace at the start of a line."""
    start = text.find(signature)
    assert start != -1, f"{signature} is not defined"
    end = text.find("\n}", start)
    return flat(text[start:end])


# --------------------------------------------------------------------------
# 1: the two names
# --------------------------------------------------------------------------

def test_the_engine_and_the_generator_name_the_two_stats_alike_and_exempt_them() -> None:
    gems = GEMS_CPP.read_text(encoding="utf-8")
    for constant, name in (("SocketFitsAnyGemStat", gen.SOCKET_FITS_ANY_GEM_STAT),
                           ("SocketedGemEffectStat", gen.SOCKETED_GEM_EFFECT_STAT)):
        assert re.findall(rf'UCataclysmGems::{constant}\s*=\s*TEXT\("([^"]+)"\)', gems) == [name], (
            f"UCataclysmGems::{constant} does not name {name!r}, the generator's name for it")
        assert name in gen.stats_with_no_attribute(), (
            f"{name} is not on UCataclysmPlayerClassStats::StatsWithNoAttribute(), so its row is refused")
    # THE CONTROL: a made-up name is on no list.
    assert "socket_fits_every_gem" not in gen.stats_with_no_attribute()


# --------------------------------------------------------------------------
# 2: the shapes the gem code would read wrongly
# --------------------------------------------------------------------------

def refusals(rows: list[dict]) -> list[str]:
    return gen.refuse_a_gem_enchantment_row_the_game_cannot_read("EnchantmentEffects", rows)


FITS_ROW = {"Name": "X#1", "Stat": gen.SOCKET_FITS_ANY_GEM_STAT, "ValueKind": "flat", "ValueLow": 1.0,
            "ValueHigh": 1.0, "RequiredTags": ""}
EFFECT_ROW = {"Name": "Y#1", "Stat": gen.SOCKETED_GEM_EFFECT_STAT, "ValueKind": "more", "ValueLow": -50.0,
              "ValueHigh": -50.0, "RequiredTags": ""}


@pytest.mark.parametrize("right, change, said", [
    (FITS_ROW, {"ValueKind": "more"}, "value kind 'more'"),
    (FITS_ROW, {"ValueLow": 2.0}, "stated as 1"),
    (FITS_ROW, {"ValueHigh": 0.0}, "stated as 1"),
    (FITS_ROW, {"RequiredTags": "Item.Slot.Weapon"}, "required tags 'Item.Slot.Weapon'"),
    (FITS_ROW, {"Condition": "while_stationary"}, "condition 'while_stationary'"),
    (EFFECT_ROW, {"ValueKind": "increased"}, "value kind 'increased'"),
    (EFFECT_ROW, {"ValueLow": -100.0}, "above -100 and not nought"),
    (EFFECT_ROW, {"ValueHigh": 0.0}, "above -100 and not nought"),
    (EFFECT_ROW, {"Scale": "max_health"}, "scale 'max_health'"),
    (EFFECT_ROW, {"Action": "strike_target"}, "action 'strike_target'"),
])
def test_every_shape_the_gem_code_would_read_wrongly_is_refused(right: dict, change: dict, said: str) -> None:
    assert refusals([right]) == [], "the right row is refused"
    problems = refusals([dict(right, **change)])
    assert len(problems) == 1 and said in problems[0], (change, problems)


def test_another_stats_row_and_a_passive_nodes_row_are_left_alone() -> None:
    assert refusals([dict(EFFECT_ROW, Stat="max_health", ValueKind="increased")]) == []
    assert refusals([dict(EFFECT_ROW, ValueKind="increased", Node="basic_x")]) == []


# --------------------------------------------------------------------------
# 3: the two real sentences, through the generator
# --------------------------------------------------------------------------

def made_up_sheet(tmp_path: pathlib.Path, effect_rows: list[dict]) -> list[dict]:
    """A made-up workbook holding the two real sentences and `effect_rows` under them, run through the generator."""
    book = openpyxl.Workbook()
    sentences = book.active
    sentences.title = "Enchantments"
    for row in (["Positives", "Type", "Weight", "Column 4", "Rolls Down", "Negatives", "Type", "Weight", "Tags",
                 "Rolls Down"],
                [FITS_ANY, "Generic", 3, "Item.Slot.Weapon", None, LESS_EFFECTIVE, "Generic", 3, None, None]):
        sentences.append(row)
    effects = book.create_sheet("Enchantment Effects")
    effects.append(EFFECTS_HEADER)
    for values in effect_rows:
        effects.append([values.get(column) for column in EFFECTS_HEADER])
    path = tmp_path / "gem_enchantments.xlsx"
    book.save(path)
    return gen.enchantment_effects(openpyxl.load_workbook(path))


def the_two_rows(**less) -> list[dict]:
    return [{"Enchantment": gen.row_name("Positive", FITS_ANY[:48]), "Effect": FITS_ANY,
             "Stat": gen.SOCKET_FITS_ANY_GEM_STAT, "Value Kind": "flat", "Value Low": 1},
            dict({"Enchantment": gen.row_name("Negative", LESS_EFFECTIVE[:48]), "Effect": LESS_EFFECTIVE,
                  "Stat": gen.SOCKETED_GEM_EFFECT_STAT, "Value Kind": "more", "Value Low": -50}, **less)]


def test_the_two_rows_carry_through_the_generator_unrefused(tmp_path: pathlib.Path) -> None:
    out = made_up_sheet(tmp_path, the_two_rows())
    assert [(row["Name"], row["Stat"], row["ValueKind"], row["ValueLow"], row["ValueHigh"], row["RequiredTags"])
            for row in out] == [
        ("Positive_Sockets_on_this_equipment_fit_any_gem_type#1", "socket_fits_any_gem", "flat", 1.0, 1.0, ""),
        ("Negative_Gems_in_this_equipment_are_50_less_effective#1", "socketed_gem_effect", "more", -50.0, -50.0, "")]
    assert gen.validate_enchantment_effects({"EnchantmentEffects": out}, set()) == []


def test_the_whole_sheets_validation_refuses_a_wrong_row(tmp_path: pathlib.Path) -> None:
    """Not only the function called by hand. THE CONTROL is the test above, where the right rows pass."""
    out = made_up_sheet(tmp_path, the_two_rows(**{"Value Kind": "increased"}))
    problems = gen.validate_enchantment_effects({"EnchantmentEffects": out}, set())
    assert any("socketed_gem_effect" in problem and "'increased'" in problem for problem in problems), problems


# --------------------------------------------------------------------------
# 4: fit-any, asked at socket time and at apply time, and never written
# --------------------------------------------------------------------------

def test_the_forge_asks_the_piece_whether_its_sockets_fit_any_gem() -> None:
    socketing = body(code(FORGE_CPP), "FCataclysmForgeSocketing UCataclysmForge::SocketGemOf(")
    assert "UCataclysmGems::GemFitsIn(Item, Socket, GemType, UCataclysmItemModifiers::LoadEnchantmentEffectTable())" \
        in socketing, "the Forge's socket check does not ask the piece's own enchantment (Q1)"
    assert "UCataclysmGems::GemFits(Into.Type" not in socketing, "the Forge's socket check reads the socket's type alone"
    listing = body(code(LAYOUT_CPP), "TArray<FCataclysmForgeSocketAction> UCataclysmForgeLayout::SocketActionsFor(")
    assert "UCataclysmGems::FitsAnyGem(Item, EffectTable)" in listing and (
        "bFitsAnyGem || UCataclysmGems::GemFits(Socket.Type, Row->GemType)" in listing), (
        "the Forge screen does not offer an off-type gem for a piece whose sockets fit any gem")


def test_a_gem_that_fits_neither_its_socket_nor_the_piece_does_nothing() -> None:
    gems = code(GEMS_CPP)
    adding = body(gems, "void UCataclysmGems::AddSocketedGems(")
    skip = adding.find("if (!bFitsAnyGem && !GemFits(Socket.Type, Row->GemType)) { continue; }")
    assert -1 < skip < adding.find("AddGemModifier("), (
        "AddSocketedGems adds a gem that fits neither its socket nor the piece (Q2)")
    assert "const bool bFitsAnyGem = FitsAnyGem(Item, EnchantmentEffectTable);" in adding
    accumulating = body(code(ITEM_CPP), "void UCataclysmItemModifiers::AccumulateInto(")
    assert ("UCataclysmGems::AddSocketedGems(Item, UCataclysmGems::LoadGeneratedTable(), "
            "LoadEnchantmentEffectTable(), Totals);") in accumulating, (
        "a worn piece's gems are added without its enchantment effect table, so neither enchantment acts")
    lines = body(gems, "TArray<FString> UCataclysmGems::SocketLinesFor(const FCataclysmItem& Item, const UDataTable*")
    assert '" (does not fit: inactive)"' in lines and '"Socket for any gem (enchanted)"' in lines, (
        "the tooltip does not say which sockets fit any gem, or which gem is inactive (Q8)")


def test_nothing_writes_any_into_a_socket() -> None:
    """Q1: the enchantment is read, never stored. A socket keeps the type it rolled."""
    writing = sorted(
        path.relative_to(SOURCE).as_posix() for path in SOURCE.rglob("*.cpp")
        if "/Tests/" not in path.as_posix()
        and re.search(r"\.\s*Type\s*=\s*ECataclysmSocketType::Any\b", code(path)))
    assert writing == [], f"{writing} write Any into a socket"
    readers = sorted(
        path.relative_to(SOURCE).as_posix() for path in SOURCE.rglob("*.*")
        if "/Tests/" not in path.as_posix() and path.suffix in (".cpp", ".h")
        and "SocketFitsAnyGemStat" in code(path))
    assert readers == ["Items/CataclysmGems.cpp", "Items/CataclysmGems.h"], (
        f"{readers} name the fit-any stat; only the gem code reads it, off the piece")


# --------------------------------------------------------------------------
# 5: the gem effect
# --------------------------------------------------------------------------

def test_the_gem_effect_multiplies_each_gems_value_once_row_by_row() -> None:
    gems = code(GEMS_CPP)
    effect = body(gems, "float UCataclysmGems::SocketedGemEffectOf(")
    assert "Effect *= 1.0f + Value / 100.0f;" in effect, "a gem effect row does not multiply (Q3)"
    assert "return FMath::Max(0.0f, Effect);" in effect, "a gem effect may fall below nought"
    assert "Round" not in effect, "the gem effect is rounded (Q4)"
    adding = body(gems, "bool UCataclysmGems::AddGemModifier(")
    assert "SheetValueAt(Row, Rarity) * Effect" in adding, "the gem effect does not scale the value before its bucket"
    socketed = body(gems, "void UCataclysmGems::AddSocketedGems(")
    assert "const float Effect = SocketedGemEffectOf(Item, EnchantmentEffectTable);" in socketed
