"""The game's Overwhelm is the simulation's, and it reaches every hit a creature lands on a player.

WHY THIS EXISTS. Issue #2824, slice b, ruled by the coordinating session on 2026-11-22. `UCataclysmPowerScore::
OverwhelmFor` (`game/Source/Cataclysm/Player/CataclysmPowerScore.cpp`) is a port of `overwhelm` in
`sim/cataclysm_sim/combat.py`, the model the design's Overwhelm figures were tuned on. Continuous integration compiles
no C++, so the automation tests `Cataclysm.Overwhelm.` never run on a pull request; they hold the two constants and the
two worked figures written out, and this check holds those to the simulation. Two copies of a number are two numbers.

WHAT IS CHECKED:

  1. `OverwhelmRate` and `OverwhelmCap` in `CataclysmPowerScore.h` are the simulation's `OVERWHELM_RATE` and
     `OVERWHELM_CAP`;
  2. the automation test's two worked figures, 8.4093 and 20.8989 points, are what the simulation's `overwhelm` gives a
     tier 8 player at 6,327 against a Common and a Cataclysm Boss on the last floor of a 50 floor Cataclysm dungeon;
  3. the wiring no automation test runs: the vital attribute set fills `Hit.OverwhelmPoints` from
     `OverwhelmPointsAgainst` with the defending body (a player's set lives on its player state, not its character),
     `Resolve` subtracts it beside penetration inside the cap's call, and the floor panel shows `OverwhelmText`.
"""

from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
HEADER = SOURCE / "Player" / "CataclysmPowerScore.h"
TEST = SOURCE / "Tests" / "CataclysmOverwhelmTests.cpp"

if str(REPO_ROOT / "sim") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "sim"))

from cataclysm_sim import combat, scoring  # noqa: E402


def _constant(name: str) -> float:
    text = HEADER.read_text(encoding="utf-8")
    found = re.findall(rf"static constexpr float {name} = ([0-9.]+)f;", text)
    assert len(found) == 1, f"{name} is declared {len(found)} times in {HEADER.name}"
    return float(found[0])


def test_the_two_constants_are_the_simulations():
    assert _constant("OverwhelmRate") == combat.OVERWHELM_RATE
    assert _constant("OverwhelmCap") == combat.OVERWHELM_CAP


def test_the_worked_figures_are_the_simulations():
    scores = scoring.enemy_scores(8, "Cataclysm", "None", 50, 50, 0.0)
    player = scoring.PLAYER_MAX_SCORES[8]
    width = scoring.tier_width(8)
    assert (player, width, scores["Common"], scores["Cataclysm Boss"]) == (6327, 1207, 6733, 7336)

    text = TEST.read_text(encoding="utf-8")
    for rarity, literal in (("Common", "8.4093f"), ("Cataclysm Boss", "20.8989f")):
        points = 100.0 * combat.overwhelm(player, scores[rarity], width)
        assert f"{points:.4f}f" == literal, (rarity, points)
        assert text.count(literal) == 1, f"the automation test no longer holds {literal}"


def test_a_creatures_blow_on_a_player_carries_it():
    text = (SOURCE / "AbilitySystem" / "CataclysmVitalAttributeSet.cpp").read_text(encoding="utf-8")
    at = text.find("Hit.OverwhelmPoints = UCataclysmPowerScore::OverwhelmPointsAgainst(")
    assert at != -1, "the vital set no longer fills the hit's Overwhelm"
    call = text[at:text.find(";", at)]
    assert "Cast<ACataclysmPlayerCharacter>(CataclysmDefendingBody(*this))" in call, call
    assert "Cast<ACataclysmEnemyCharacter>(Instigator->GetAvatarActor())" in call, call


def test_resolve_takes_it_beside_penetration_before_the_cap():
    text = (SOURCE / "AbilitySystem" / "CataclysmDamageCalculation.cpp").read_text(encoding="utf-8")
    # AND A CRITICAL STRIKE'S OWN RESISTANCE PENETRATION BESIDE THEM, Glass Cannon (the War trees' engine slice E2,
    # 2026-11-27), spelled out: the overwhelm is still beside the penetration and still before the cap.
    assert re.search(r"Hit\.ResistancePenetration \+ Hit\.OverwhelmPoints"
                     r"(?:\s*//[^\n]*\n\s*\+ \(Result\.bWasCritical \? Hit\.CriticalResistancePenetration : 0\.0f\))?"
                     r",\s*ResistanceCapOf\(Defender\)\);", text)
    # ARMOUR TAKES IT THROUGH ONE LOCAL SINCE THE RAVAGER TREE REWORK OF
    # 2026-11-24: the whole of the blow's points, or half of them for a defender
    # holding Ironhide's `armor_overwhelm_halved` (the owner's ruling that day).
    # Resistance, evasion and block still take the whole, asserted above and
    # below.
    assert "ArmorReduction(Armor, Tier) - OverwhelmOnArmour" in text
    assert ("bOverwhelmHalvedOnArmour ? Hit.OverwhelmPoints * 0.5f : Hit.OverwhelmPoints"
            in text)
    # THE BLOCK'S SINCE THE WAR TREES' ENGINE SLICE E3 (2026-11-27) is taken off the asked chance in a local, so Eye of
    # the Storm's floor can be held against the reduction: `AskedBlockChance - Hit.OverwhelmPoints);`.
    assert text.count("- Hit.OverwhelmPoints))") == 1, "evasion takes it off its chance"
    assert text.count("FMath::Max(0.0f, AskedBlockChance - Hit.OverwhelmPoints);") == 1, "and block takes it off its"


def test_the_floor_panel_shows_it():
    text = (SOURCE / "Dungeon" / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    assert "UCataclysmPowerScore::OverwhelmText(PlayerScore, ThisFloor)" in text
