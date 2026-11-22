"""A thrall sets alight what it strikes, and a minion's burn grows with its skill's level: issue #2784, ruled by the
coordinating session on 2026-11-22.

Subjugate's row: "...fights for you until it dies, keeps its own abilities, and sets alight what it strikes." A thrall's
blows all pass through `UCataclysmSkillEffects::ApplyHit`, which now lays the summoned imp's kind of burn after a landed
blow by a creature the player commands (Q1 to Q3, Q6). And a minion's or a thrall's burn takes its skill's level factor
through `ApplyBurn`'s `LevelFactor` (Q4, derived from the owner's answer of 2026-11-19, "Yes, they scale too"): a burn is
flat, not a share of the blow, so until this a minion's burn took no factor at all.

WHAT EACH CHECK HOLDS, and what would go wrong without it:

  1. `ApplyHit` lays the burn after the blow is dealt, only for a landed blow that is no share on an ally and no tick,
     with the imp's arguments, the row of the skill that took the thrall and the factor its blow took -- without it the
     row's sentence is false again;
  2. a thrall is a creature whose commander is the PLAYER, so a taken Gatekeeper's imps lay none (Q2);
  3. `ApplyBurn` multiplies the burn's figure by `LevelFactor`, never below nought;
  4. both of the imp's burns pass their summoning skill's factor, on a creature;
  5. every burn laid unscaled by its applier names a level factor, so a new minion burn cannot be added without one.
"""
from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ABILITY = REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
EFFECTS_CPP = ABILITY / "CataclysmSkillEffects.cpp"
MINION_CPP = ABILITY / "CataclysmMinion.cpp"
SOURCE = REPO_ROOT / "game" / "Source"


def code(path: pathlib.Path) -> str:
    """The file with its comments removed, so a name written only in a comment does not count."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*(?!\s*\w+=\s*\*/).*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def body(text: str, signature: str) -> str:
    start = text.find(signature)
    assert start != -1, f"{signature} is not defined"
    return flat(text[start:text.find("\n}", start)])


def apply_burn_calls(text: str) -> list[str]:
    """Every `ApplyBurn(` call in `text`, to its closing parenthesis, flattened."""
    calls = []
    for match in re.finditer(r"ApplyBurn\(", text):
        depth, index = 0, match.end() - 1
        while index < len(text):
            depth += {"(": 1, ")": -1}.get(text[index], 0)
            if depth == 0:
                break
            index += 1
        calls.append(flat(text[match.start():index + 1]))
    return calls


def test_apply_hit_lays_the_burn_after_a_landed_blow_by_a_thrall() -> None:
    hit = body(code(EFFECTS_CPP), "float UCataclysmSkillEffects::ApplyHit(")
    dealt = hit.find("const bool bDealt = ApplyDirectDamage(Instigator, Target, Sent, Arrived, Landed);")
    landed = hit.find("if (!bAShareOnAnAlly && !Arrived.bIsDamageOverTime && !Landed->bEvaded "
                      "&& !Landed->bRefusedByImmunityToAttacker)")
    thrall = hit.find("CataclysmSkillEffectsLevels::PlayersThrall(Instigator)")
    burn = hit.find("ApplyBurn(Instigator, Target, Sent, /*bScalesWithInstigator=*/false, /*bBurnIsDesigned=*/true, "
                    "/*DealtBy=*/Instigator, /*Skill=*/nullptr, /*SkillTagsWithNoSkill=*/nullptr, "
                    "/*SkillRowWithNoSkill=*/Thrall->TakenBySkillRow, "
                    "/*LevelFactor=*/CataclysmSkillEffectsLevels::ThrallsLevelFactor(Instigator, Target));")
    assert -1 < dealt < landed < thrall < burn, (
        "ApplyHit does not lay the imp's kind of burn after a landed blow by a thrall, with its taker's row and factor "
        f"(positions {dealt}, {landed}, {thrall}, {burn})")


def test_a_thrall_is_a_creature_the_player_commands() -> None:
    helper = body(code(EFFECTS_CPP), "const ACataclysmEnemyCharacter* PlayersThrall(const AActor* Instigator)")
    assert ("Cast<const ACataclysmPlayerCharacter>(UCataclysmCommand::CommanderOf(Thrall))" in helper
            and "CommandedThrall(Instigator)" in helper), (
        "a thrall's burn is not limited to a creature the player commands, so a taken Gatekeeper's imps would burn (Q2)")


def test_apply_burn_multiplies_its_figure_by_the_level_factor() -> None:
    burn = body(code(EFFECTS_CPP), "bool UCataclysmSkillEffects::ApplyBurn(")
    assert "Burn.DamagePerTickAgainst(HitDamage) * FMath::Max(0.0f, LevelFactor)" in burn, (
        "ApplyBurn does not multiply the burn's figure by LevelFactor (Q4)")


def test_both_of_the_imps_burns_take_their_summoning_skills_factor() -> None:
    calls = apply_burn_calls(code(MINION_CPP))
    assert len(calls) == 2, f"the minion lays {len(calls)} burns; this check expects its swing's and its explosion's"
    for call in calls:
        assert ("/*LevelFactor=*/Cast<const ACataclysmEnemyCharacter>(Target) ? LevelFactorOnCreatures : 1.0f"
                in call), f"an imp's burn takes no level factor: {call}"


def test_every_burn_unscaled_by_its_applier_names_a_level_factor() -> None:
    missing = []
    for path in SOURCE.rglob("*.cpp"):
        if "/Tests/" in path.as_posix():
            continue
        for call in apply_burn_calls(code(path)):
            if "/*bScalesWithInstigator=*/false" in call and "/*LevelFactor=*/" not in call:
                missing.append(f"{path.name}: {call[:120]}")
    assert not missing, "a burn laid unscaled by its applier takes no level factor:\n" + "\n".join(missing)
    # THE CONTROL: the sweep finds the three it is meant to find.
    found = sum(1 for path in SOURCE.rglob("*.cpp") if "/Tests/" not in path.as_posix()
                for call in apply_burn_calls(code(path)) if "/*bScalesWithInstigator=*/false" in call)
    assert found == 3, f"{found} burns unscaled by their applier were found; the imp's two and the thrall's are 3"
