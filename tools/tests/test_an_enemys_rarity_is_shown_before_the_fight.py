"""An enemy's rarity is drawn over it, and it does not wait to be hurt first.

WHY THIS EXISTS. Issue #740. A Common enemy and a Boss looked identical: same
model, same size, same colour, no name. Rarity changes three things and every one
was invisible until the creature was dead -- how many items the kill drops, the
magic find it adds to its own drops, and whether it can be stunned at all. The
project owner found it by playing.

WHY IT IS CHECKED FROM PYTHON. Continuous integration compiles no C++, so the
automation tests in `game/Source/Cataclysm/Tests/CataclysmEnemyRarityTests.cpp`
never run on a pull request. Reading the source as text does. That is the same
arrangement `tools/tests/test_carried_inventory_is_forty_eight_slots.py` uses,
and its header says why.

THE ONE THING WORTH GUARDING ABOVE ALL. **A rarity is shown before the fight, not
during it.** The health bar over a creature deliberately waits until it has been
hurt, which is right for a bar and wrong for a rarity: the design's rule that a
boss cannot be stunned at all is worth nothing to a player who finds out by
spending the stun. It would be an easy and invisible mistake to reuse the bar's
condition, and the result would look like it worked -- the word would appear,
just always too late.

WHAT IS NOT CHECKED HERE. Anything about how it looks, where it sits over the
creature, or whether it can be read at a glance. Nothing that reaches the screen
can be watched by any test in this project: the automation command in
`tools/unreal_build.py` passes `-nullrhi`.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
HUD_CPP = SOURCE / "Interface" / "CataclysmHUD.cpp"
OVERLAY_H = SOURCE / "Interface" / "CataclysmCombatOverlay.h"
OVERLAY_CPP = SOURCE / "Interface" / "CataclysmCombatOverlay.cpp"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: The six rungs, from `RARITY_ORDER` in sim/cataclysm_sim/enemy_stats.py.
RUNGS = ("Common", "Elite", "Legendary", "Herald", "Boss", "Cataclysm Boss")


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. Showing "
            f"an enemy's rarity is issue #740; if a file was renamed, rename it "
            f"here too.")
    return path.read_text(encoding="utf-8")


def flat(path: pathlib.Path) -> str:
    """The file as one line, so a sentence broken across a wrap still matches."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    return " ".join(path.read_text(encoding="utf-8").split())


def function_body(text: str, signature: str) -> str:
    """A function's code, with its comments stripped out.

    THE COMMENTS HAVE TO GO OR THE CHECKS BELOW READ THEM. Every rule in this
    file is written down beside the code that implements it, in a comment that
    names the very thing being searched for -- the first version of
    `test_it_is_shown_before_the_creature_has_been_hurt` failed on the sentence
    "the same reason ShouldShowBarFor gives", which is an explanation rather
    than a call. Found by running it.
    """
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}\n", start)
    assert end != -1, f"{signature!r} is not closed"

    body = text[start:end]
    body = re.sub(r"/\*.*?\*/", " ", body, flags=re.DOTALL)
    body = re.sub(r"//[^\n]*", " ", body)
    return body


def test_the_heads_up_display_draws_it() -> None:
    hud = read(HUD_CPP)

    assert "DrawRarityNames();" in hud, (
        "CataclysmHUD.cpp never calls DrawRarityNames, so an enemy's rarity is "
        "never drawn however it is decided. Issue #740.")

    assert "UCataclysmEnemyRarity::RarityNameForStep" in hud, (
        "CataclysmHUD.cpp does not ask UCataclysmEnemyRarity what a rung is "
        "called, so the words are coming from somewhere else. The names belong "
        "to game/Data/EnemyRarities.csv, which is generated from the model.")


def test_it_does_not_write_the_rung_names_a_second_time() -> None:
    """The words come from the table, not from a list in the drawing code.

    A COPY WOULD BE A SECOND LADDER. `game/Data/EnemyRarities.csv` carries every
    rung and its name, generated from `RARITY_ORDER` in
    `sim/cataclysm_sim/enemy_stats.py`, and a list written into the heads-up
    display could drift from it silently -- a rung renamed in the model would
    still be drawn by its old name.
    """
    hud = read(HUD_CPP)

    written = [rung for rung in RUNGS if f'TEXT("{rung}")' in hud]
    assert not written, (
        f"CataclysmHUD.cpp writes these rarity names as literals: {written}. "
        f"They belong to game/Data/EnemyRarities.csv; ask "
        f"UCataclysmEnemyRarity::RarityNameForStep instead.")


def test_it_has_its_own_toggle_rather_than_the_health_bars() -> None:
    """A bar and a rarity answer different questions and are switched apart.

    Sharing the bar's console variable would mean a person turning off the bars
    to see the fight also lost the only thing telling them what they are
    fighting.
    """
    hud = read(HUD_CPP)

    assert "RarityNamesEnabled()" in hud, (
        "CataclysmHUD.cpp does not check RarityNamesEnabled, so the rarity is "
        "drawn under whatever switch the health bars use.")

    assert "RarityNamesEnabled" in read(OVERLAY_H), (
        "UCataclysmCombatOverlay no longer declares RarityNamesEnabled.")

    assert "Cataclysm.Overlay.RarityNames" in read(OVERLAY_CPP), (
        "There is no Cataclysm.Overlay.RarityNames console variable, so the "
        "rarity cannot be switched off on its own.")


def test_it_is_shown_before_the_creature_has_been_hurt() -> None:
    """The load-bearing rule, and the one an easy mistake would break.

    WHAT THE MISTAKE LOOKS LIKE. Reusing `ShouldShowBarFor`, or copying its last
    line, gives a rarity that appears only once the creature has been damaged.
    Everything still draws, so it reads as working; it is simply always too
    late to be worth anything.
    """
    overlay = read(OVERLAY_CPP)
    body = function_body(overlay, "bool UCataclysmCombatOverlay::ShouldShowRarityNameFor")

    assert "ShouldShowBarFor" not in body, (
        "ShouldShowRarityNameFor calls ShouldShowBarFor, which refuses to show "
        "anything over an undamaged creature. A rarity has to be readable "
        "before the fight starts, which is the whole reason it is a separate "
        "rule. Issue #740.")

    assert not re.search(r"Health\s*<\s*MaxHealth", body), (
        "ShouldShowRarityNameFor tests whether the creature has been hurt. That "
        "is the health bar's rule and it is the wrong one here: a rarity found "
        "out after the fight started tells the player what they already know.")

    # AND IT STILL REFUSES A CORPSE, which is the half it does share with the
    # bar. Without it the word flashes for one frame at the end of every fight.
    #
    # A WORD BOUNDARY, BECAUSE `MaxHealth <= 0.0f` CONTAINS `Health <= 0.0f`.
    # The plain substring passed a build with the corpse check deleted, since
    # the check for a creature with no health pool at all was still there and
    # matched it. Found by breaking this guard on purpose.
    assert re.search(r"\bHealth\s*<=\s*0", body), (
        "ShouldShowRarityNameFor no longer refuses a dead creature. An enemy "
        "destroys itself on the tick after it dies, so the word would flash "
        "over a corpse for one frame at the end of every fight.")


def test_a_common_enemy_is_not_marked() -> None:
    """Marking every creature would mark none of them.

    Common is 60% of what spawns, from the Dungeon Score Formula section of the
    design document, so a word over every one of them is a word over most of the
    screen.
    """
    overlay_header = read(OVERLAY_H)

    found = re.search(
        r"static\s+constexpr\s+int32\s+LowestMarkedRarityStep\s*=\s*(-?\d+)\s*;",
        overlay_header)
    assert found, (
        "UCataclysmCombatOverlay has no LowestMarkedRarityStep. It is what "
        "keeps a word off every Common enemy on the screen.")

    assert int(found.group(1)) == 1, (
        f"LowestMarkedRarityStep is {found.group(1)}. Common is rung 0 and is "
        f"not marked, so the lowest marked rung is 1, which is Elite.")

    body = function_body(read(OVERLAY_CPP),
                         "bool UCataclysmCombatOverlay::ShouldShowRarityNameFor")
    assert "LowestMarkedRarityStep" in body, (
        "ShouldShowRarityNameFor does not use LowestMarkedRarityStep, so a "
        "Common enemy is marked like everything else.")


def test_the_design_document_records_the_rule() -> None:
    """A design decision is not real until it is in docs/, per CLAUDE.md."""
    text = flat(GDD)

    assert "A rarity above Common is said in a word over the creature" in text, (
        "docs/Cataclysm_GDD_v2.md no longer says that an enemy's rarity is "
        "shown as a word over it. That is the decision this code implements.")

    assert "It appears before the fight, not during it" in text, (
        "docs/Cataclysm_GDD_v2.md no longer records that an enemy's rarity is "
        "shown before the fight rather than during it. That is the rule the "
        "whole feature rests on.")
