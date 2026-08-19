"""A panel at the top of the screen says what the creature under the cursor is.

WHY THIS EXISTS. Issue #740, second half. The word over a creature's head says
which creature in a pack to look at. It has no room to say what that creature
is, and the thing a player most needs is exactly what will not fit: the design
gives an enemy one modifier per rung above Common, up to five for a Cataclysm
Boss, and those are mechanical effects that change how it has to be fought.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in `game/Source/Cataclysm/Tests/CataclysmCreaturePanelTests.cpp`
never run on a pull request. Reading the source as text does. That is the same
arrangement `tools/tests/test_an_enemys_rarity_is_shown_before_the_fight.py`
uses, and its header says why.

THE THREE THINGS MOST WORTH GUARDING, all of which would look like working code:

- **A Common creature gets a panel.** That is the opposite of the rule the word
  over the head follows, where a Common is left bare because it is 60% of what
  spawns. Copying the word's rule here would leave most of the game silent when
  pointed at, and nothing would report it.

- **The cursor is traced against pawn OBJECTS, not the visibility channel.** The
  engine's stock `Pawn` and `CharacterMesh` collision profiles both set
  `Visibility` to Ignore, so the visibility trace the click-to-move code uses
  passes straight through every creature in the game. A panel built on that
  trace would simply never find anything, and the code would read as correct.

- **The words come from the generated tables.** A rung or a creature renamed in
  the design workbook has to be renamed on screen, and a list of names written
  into the drawing code would drift silently.

WHAT IS NOT CHECKED HERE. Anything about how it looks. Its colours are measured
by `tools/tests/test_the_creature_panel_is_readable.py`; whether the result reads
at a glance needs a person, because the automation command in
`tools/unreal_build.py` passes `-nullrhi` and nothing reaches a screen under test.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
HUD_CPP = SOURCE / "Interface" / "CataclysmHUD.cpp"
PANEL_H = SOURCE / "Interface" / "CataclysmCreaturePanel.h"
PANEL_CPP = SOURCE / "Interface" / "CataclysmCreaturePanel.cpp"
ENEMY_H = SOURCE / "Character" / "CataclysmEnemyCharacter.h"
CHARACTERS = SOURCE / "Character"
ARCHETYPES = REPO_ROOT / "game" / "Data" / "EnemyArchetypes.csv"
MODIFIERS = REPO_ROOT / "game" / "Data" / "EnemyModifiers.csv"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: The six rungs, from `RARITY_ORDER` in sim/cataclysm_sim/enemy_stats.py.
RUNGS = ("Common", "Elite", "Legendary", "Herald", "Boss", "Cataclysm Boss")

#: `ArchetypeRow = TEXT("Abyssal_Warden");` in an enemy class's constructor.
NAMES_A_ROW = re.compile(r'ArchetypeRow\s*=\s*TEXT\(\s*"([^"]*)"\s*\)\s*;')


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. The "
            f"panel describing the creature under the cursor is issue #740; if "
            f"a file was renamed, rename it here too.")
    return path.read_text(encoding="utf-8")


def flat(path: pathlib.Path) -> str:
    """The file as one line, so a sentence broken across a wrap still matches."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return " ".join(path.read_text(encoding="utf-8").split())


def function_body(text: str, signature: str) -> str:
    """A function's code, with its comments stripped out.

    THE COMMENTS HAVE TO GO OR THE CHECKS BELOW READ THEM. Every rule in this
    project is written down beside the code that implements it, in a comment
    naming the very thing being searched for -- `ShouldShowFor` carries a
    comment saying it does NOT ask the rarity, which contains the word the
    check below looks for. The same trap
    `test_an_enemys_rarity_is_shown_before_the_fight.py` records.
    """
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"

    body = text[start:end]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", " ", body)
    return body


def column(path: pathlib.Path, name: str) -> list[str]:
    """One column of a generated table, in the order the rows are written."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    with open(path, encoding="utf-8-sig", newline="") as handle:
        return [row[name] for row in csv.DictReader(handle)]


# ---------------------------------------------------------------------------
# That it is drawn at all
# ---------------------------------------------------------------------------

def test_the_heads_up_display_draws_it() -> None:
    """It is called from the frame, and not merely mentioned in a comment.

    CHECKED AGAINST THE BODY OF DrawHUD WITH ITS COMMENTS STRIPPED. A plain
    search of the file passes a build where the call has been commented out,
    because `// DrawCreaturePanel();` still contains the text being searched
    for. Found by breaking this guard: it was the one break of fourteen that
    the first version of this file did not notice.
    """
    frame = function_body(read(HUD_CPP), "void ACataclysmHUD::DrawHUD")

    assert "DrawCreaturePanel();" in frame, (
        "ACataclysmHUD::DrawHUD never calls DrawCreaturePanel, so nothing "
        "describes the creature under the cursor however well it is decided. "
        "Issue #740.")

    drawn = function_body(read(HUD_CPP), "void ACataclysmHUD::DrawCreaturePanel")
    assert "CreatureUnderCursor()" in drawn, (
        "DrawCreaturePanel never asks which creature the cursor is over.")


def test_it_has_its_own_switch() -> None:
    """Turning off the damage numbers must not take the modifiers with them.

    This is the only thing in the game that will ever say what a creature's
    modifiers are, so it cannot share a console variable with anything else.

    THE SAME COMMENT-STRIPPED BODY, for the reason the test above gives.
    """
    frame = function_body(read(HUD_CPP), "void ACataclysmHUD::DrawHUD")

    assert "CreaturePanelEnabled()" in frame, (
        "ACataclysmHUD::DrawHUD does not check CreaturePanelEnabled, so the "
        "panel is drawn under whatever switch something else uses.")

    assert "Cataclysm.Overlay.CreaturePanel" in read(PANEL_CPP), (
        "There is no Cataclysm.Overlay.CreaturePanel console variable, so the "
        "panel cannot be switched off on its own.")


# ---------------------------------------------------------------------------
# Which creature it describes
# ---------------------------------------------------------------------------

def test_the_cursor_is_traced_against_creatures_rather_than_the_world() -> None:
    """The trap that would make the whole feature silently find nothing.

    WHAT GOES WRONG. `GetHitResultUnderCursor` defaults to the visibility
    channel, and ACataclysmPlayerController::UpdateCachedDestination uses it for
    click-to-move. The engine's stock `Pawn` profile sets Visibility to Ignore
    and its `CharacterMesh` profile does the same, so that trace passes through
    every creature in the game and lands on the floor behind it. Copying it here
    gives a panel that compiles, runs, and never appears.
    """
    body = function_body(
        read(HUD_CPP),
        "const ACataclysmEnemyCharacter* ACataclysmHUD::CreatureUnderCursor")

    assert "ECC_Visibility" not in body, (
        "CreatureUnderCursor traces on the visibility channel. The engine's "
        "stock Pawn and CharacterMesh collision profiles both set Visibility to "
        "Ignore, so that trace goes straight through every creature and the "
        "panel would never appear. Ask for Pawn objects instead.")

    assert "ConvertToObjectType(ECC_Pawn)" in body, (
        "CreatureUnderCursor does not ask for Pawn objects. That is what finds "
        "a creature under the cursor, and it is the same channel "
        "UCataclysmTargeting already uses to find things to hit.")


def test_a_cursor_over_an_open_screen_describes_nothing() -> None:
    """The same fault issue #731 fixed for clicks, asked about hovering.

    ANCHORED ON THE CALL RATHER THAN THE NAME. A check for the bare string
    matches the function's own declaration in the player controller's header and
    would pass a build where the caller had stopped calling it. That mistake is
    recorded in this project's notes because it has already happened once.
    """
    body = function_body(
        read(HUD_CPP),
        "const ACataclysmEnemyCharacter* ACataclysmHUD::CreatureUnderCursor")

    assert "->CursorIsOverInterface()" in body, (
        "CreatureUnderCursor does not ask the player controller whether the "
        "cursor is over an open screen. Without it, a cursor resting on an "
        "inventory cell describes whatever creature stands behind the panel.")


# ---------------------------------------------------------------------------
# Whether a panel appears, which is NOT the rarity word's rule
# ---------------------------------------------------------------------------

def test_a_common_creature_is_described() -> None:
    """The load-bearing difference from the word over the head.

    WHAT THE MISTAKE LOOKS LIKE. Reusing `ShouldShowRarityNameFor`, or copying
    its rarity floor, gives a panel that answers for Elites and above and stays
    silent for the 60% of creatures that spawn Common. Everything still draws,
    so it reads as working; it is simply mute most of the time.
    """
    body = function_body(
        read(PANEL_CPP), "bool UCataclysmCreaturePanel::ShouldShowFor")

    assert "ShouldShowRarityNameFor" not in body, (
        "ShouldShowFor calls ShouldShowRarityNameFor, which refuses a Common "
        "creature on purpose. A panel is asked for by pointing at one creature, "
        "so there is nothing to declutter and every rung has to answer. Issue "
        "#740.")

    assert "LowestMarkedRarityStep" not in body, (
        "ShouldShowFor uses LowestMarkedRarityStep, which is the floor that "
        "keeps a word off a Common creature's head. The panel has no such "
        "floor: the player pointed at the creature.")

    assert "RarityStep" not in body, (
        "ShouldShowFor looks at the creature's rarity. Whether a panel appears "
        "does not depend on the rung; what the panel SAYS does.")


def test_it_still_refuses_a_corpse() -> None:
    """The half the panel does share with the bar and the word.

    A WORD BOUNDARY, BECAUSE `MaxHealth <= 0.0f` CONTAINS `Health <= 0.0f`. The
    plain substring passes a build with the corpse check deleted, because the
    check for a creature with no health pool at all matches it. That trap is
    recorded in this project's notes; it was found by breaking the guard.
    """
    body = function_body(
        read(PANEL_CPP), "bool UCataclysmCreaturePanel::ShouldShowFor")

    assert re.search(r"\bHealth\s*<=\s*0", body), (
        "ShouldShowFor no longer refuses a dead creature. An enemy destroys "
        "itself on the tick after it dies, so the panel would flash over a "
        "corpse for one frame at the end of every fight.")


def test_a_living_creature_never_reads_zero_health() -> None:
    """Health is an unrounded float, so rounding alone lies about a survivor.

    NOT A RARE CASE. UCataclysmDamageCalculation::Resolve ends with
    FMath::Min(Damage, Health), so a killing blow leaves health at exactly what
    was there; and a creature on 0.3 health is alive, hittable, and would be
    printed as "0 / 250". That is the one thing a health readout must not say
    about something still standing. UCataclysmCombatOverlay::FigureFor exists
    for the same reason on the damage numbers.
    """
    body = function_body(
        read(PANEL_CPP), "FString UCataclysmCreaturePanel::HealthTextFor")

    assert re.search(r"FMath::Max\s*\(\s*1\s*,", body), (
        "HealthTextFor rounds the current health without a floor of 1, so a "
        "creature alive on a fraction of a point reads as 0. Issue #740.")


# ---------------------------------------------------------------------------
# Where the words come from
# ---------------------------------------------------------------------------

def test_it_does_not_write_the_rung_names_a_second_time() -> None:
    """The words come from the tables, not from a list in the drawing code."""
    hud = read(HUD_CPP)

    written = [rung for rung in RUNGS if f'TEXT("{rung}")' in hud]
    assert not written, (
        f"CataclysmHUD.cpp writes these rarity names as literals: {written}. "
        f"They belong to game/Data/EnemyRarities.csv; ask "
        f"UCataclysmEnemyRarity::RarityNameForStep instead.")


def test_it_does_not_write_the_creature_names_a_second_time() -> None:
    """A creature renamed in the design workbook is renamed on screen.

    THE PANEL READS `game/Data/EnemyArchetypes.csv`, which is generated. A name
    written into the drawing code would keep printing the old one, and nothing
    would report it.
    """
    hud = read(HUD_CPP)
    panel = read(PANEL_CPP)

    for name in column(ARCHETYPES, "ArchetypeName"):
        assert f'TEXT("{name}")' not in hud, (
            f"CataclysmHUD.cpp writes the creature name {name!r} as a literal. "
            f"It belongs to game/Data/EnemyArchetypes.csv; ask "
            f"UCataclysmCreaturePanel::ArchetypeNameForRow instead.")
        assert f'TEXT("{name}")' not in panel, (
            f"CataclysmCreaturePanel.cpp writes the creature name {name!r} as "
            f"a literal. It belongs to game/Data/EnemyArchetypes.csv.")


def test_it_does_not_write_the_modifier_names_a_second_time() -> None:
    """The same rule for the lines the panel exists to carry."""
    hud = read(HUD_CPP)
    panel = read(PANEL_CPP)

    for name in column(MODIFIERS, "ModifierName"):
        assert f'TEXT("{name}")' not in hud, (
            f"CataclysmHUD.cpp writes the modifier name {name!r} as a literal. "
            f"It belongs to game/Data/EnemyModifiers.csv.")
        assert f'TEXT("{name}")' not in panel, (
            f"CataclysmCreaturePanel.cpp writes the modifier name {name!r} as "
            f"a literal. It belongs to game/Data/EnemyModifiers.csv.")


def test_every_creature_names_a_row_the_archetype_table_holds() -> None:
    """A typo here would call the creature by the standing word instead.

    WHAT IT LOOKS LIKE WHEN IT IS WRONG. A Brute whose constructor said
    `Brutes` finds no row, so the panel calls it "Elite Creature" -- which reads
    exactly like a creature that has no archetype yet, which several of them
    genuinely do. Nothing else would report it, and continuous integration
    compiles no C++.
    """
    rows = set(column(ARCHETYPES, "Name"))
    assert rows, "game/Data/EnemyArchetypes.csv has no rows."

    named: dict[str, str] = {}
    for source in sorted(CHARACTERS.glob("*.cpp")):
        for row in NAMES_A_ROW.findall(source.read_text(encoding="utf-8")):
            named[source.name] = row

    assert named, (
        "No enemy class sets ArchetypeRow, so every creature in the game is "
        "described by the standing word rather than by its name. Issue #740.")

    for source, row in sorted(named.items()):
        assert row in rows, (
            f"{source} sets ArchetypeRow to {row!r}, which is not a row of "
            f"game/Data/EnemyArchetypes.csv. The rows are {sorted(rows)}. The "
            f"panel would call this creature by "
            f"UCataclysmCreaturePanel::UnnamedCreature instead.")


def test_the_enemy_class_says_a_modifier_grants_no_effect_yet() -> None:
    """The gap is written beside the field rather than found in play.

    NOTHING GRANTS AN ENEMY A MODIFIER. `ModifierRows` is a list of names the
    panel prints, and a creature given Hellfire Aura by hand does not burn
    anybody, because the aura does not exist. Saying so on the field is what
    stops the next person reading the list as a working feature.
    """
    text = flat(ENEMY_H)

    assert "TArray<FName> ModifierRows;" in text, (
        "ACataclysmEnemyCharacter has no ModifierRows, so nothing can give a "
        "creature a modifier even by hand and the panel's modifier lines can "
        "never be seen. Issue #740.")

    assert "NOTHING GRANTS THE EFFECT YET" in text, (
        "The comment on ACataclysmEnemyCharacter::ModifierRows no longer says "
        "that a modifier grants no mechanical effect. It does not: the list is "
        "names the panel prints. Somebody will otherwise read it as working.")


# ---------------------------------------------------------------------------
# Where it goes
# ---------------------------------------------------------------------------

def test_the_panel_has_a_width_floor_so_it_does_not_jitter() -> None:
    """Health figures change width every time a digit does.

    A panel sized only to its contents breathes in and out through a fight,
    because "250 / 250" and "9 / 250" are different widths. The floor is a share
    of the viewport rather than a number of pixels so it holds on any screen.
    """
    header = read(PANEL_H)

    found = re.search(
        r"static\s+constexpr\s+float\s+MinimumWidthShare\s*=\s*([\d.]+)f\s*;",
        header)
    assert found, (
        "UCataclysmCreaturePanel has no MinimumWidthShare. It is what stops the "
        "panel changing width every time the creature's health loses a digit.")

    share = float(found.group(1))
    assert 0.0 < share < 1.0, (
        f"MinimumWidthShare is {share}. It is a share of the viewport's width, "
        f"so it has to be between 0 and 1; a value of 1 or more would make "
        f"every panel the full width of the screen.")


# ---------------------------------------------------------------------------
# The design
# ---------------------------------------------------------------------------

def test_the_design_document_records_what_the_panel_shows() -> None:
    """A design decision is not real until it is in docs/, per CLAUDE.md."""
    text = flat(GDD)

    assert "A panel at the top of the screen shows the rest, on hover" in text, (
        "docs/Cataclysm_GDD_v2.md no longer says that a panel at the top of the "
        "screen describes the creature under the cursor.")

    assert "its name, its rarity, its health and its modifiers" in text, (
        "docs/Cataclysm_GDD_v2.md no longer records what the hover panel shows. "
        "The modifiers are the reason it exists; a word over a creature's head "
        "has no room for them.")
