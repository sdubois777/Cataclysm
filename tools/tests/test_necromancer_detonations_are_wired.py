"""The Necromancer's detonations reach the game: each hook is in its function, in the order that makes it work.

The finishing of the Death Cataclysm, slice C (2026-11-26): the Charnel limb and the vows Living Bombs and The Horde
Ascendant. `UCataclysmDetonation` and `UCataclysmHordeAscendant` hold the mechanisms; these are the places the game
calls them, each a line or two in a function another slice also edits.

WHY IT IS CHECKED FROM PYTHON AS WELL. Continuous integration compiles no C++, so the automation tests in
`Cataclysm.NecromancerDetonation.` never run on a pull request. Reading the source does.

WHAT IS NOT CHECKED HERE. Whether the mechanisms work: that is what those tests are for. This file checks that each call
is present, in its function, where it has to be, and not in a comment.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO_ROOT / "game" / "Source" / "Cataclysm"
ABILITY = SOURCE / "AbilitySystem"
SKILL_TEMPLATE = ABILITY / "CataclysmSkillTemplate.cpp"
SKILL_TEMPLATES = ABILITY / "CataclysmSkillTemplates.cpp"
MINION = ABILITY / "CataclysmMinion.cpp"
COMMAND = ABILITY / "CataclysmCommand.cpp"
DETONATION = ABILITY / "CataclysmDetonation.cpp"
SYSTEM = ABILITY / "CataclysmAbilitySystemComponent.cpp"
PLAYER = SOURCE / "Character" / "CataclysmPlayerCharacter.cpp"


def body_of(path: pathlib.Path, signature: str) -> str:
    """The body of the function whose definition is `signature(`, with every line comment removed, because the
    paragraphs above each call name the very calls checked here."""
    if not path.is_file():
        pytest.skip(f"{path.name} is not present")
    text = path.read_text(encoding="utf-8")
    opening = text.find(f"{signature}(")
    assert opening != -1, f"{path.name} no longer defines {signature}; if it was renamed, rename it here"
    brace = text.find("{", opening)
    depth = 0
    end = brace
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                end = index
                break
    return "\n".join(re.sub(r"//.*$", "", line) for line in text[brace:end].splitlines())


def position(body: str, needle: str, where: str) -> int:
    found = body.find(needle)
    assert found != -1, f"{where} no longer holds `{needle}`"
    return found


def test_the_key_is_asked_around_the_minions_and_no_call_site_asks_it():
    """THE COORDINATING SESSION'S REQUIREMENT OF 2026-11-26: every weapon skill's landing reaches
    `DetonateOwnMinionsWhere` (slice D's remains are detonated there), and the row's `Detonates` key, or Lit Fuse, is
    asked inside it before any minion is gathered. So the call sites in `CataclysmSkillTemplates.cpp` ask nothing."""
    body = body_of(SKILL_TEMPLATE, "int32 UCataclysmSkillTemplate::DetonateOwnMinionsWhere")
    asked = position(body, "if (!DetonatesOwnMinions())", "DetonateOwnMinionsWhere")
    gathered = position(body, "ThingsCommandedBy(Self)", "DetonateOwnMinionsWhere")
    assert asked < gathered, "the key is asked after the minions are gathered"
    position(body, "UCataclysmDetonation::Detonate(Minion, this)", "DetonateOwnMinionsWhere")
    templates = "\n".join(re.sub(r"//.*$", "", line) for line in SKILL_TEMPLATES.read_text(encoding="utf-8").splitlines())
    assert "Params.bDetonates" not in templates, (
        "a call site in CataclysmSkillTemplates.cpp asks the Detonates key again; it is asked in DetonateOwnMinionsWhere")


def test_lit_fuse_is_the_row_key_or_a_weapon_skill_the_caster_uses_itself():
    body = body_of(SKILL_TEMPLATE, "bool UCataclysmSkillTemplate::DetonatesOwnMinions")
    position(body, "Params.bDetonates", "DetonatesOwnMinions")
    position(body, "Slot != ECataclysmAbilitySlot::BasicAttack", "DetonatesOwnMinions")
    position(body, "FreeRepeatCastBy.IsExplicitlyNull()", "DetonatesOwnMinions")
    position(body, "UCataclysmDetonation::WeaponSkillsDetonateOwnMinions(Avatar())", "DetonatesOwnMinions")


def test_a_detonation_takes_its_own_damage_and_area():
    body = body_of(MINION, "float ACataclysmMinion::ExplodeSetOffBy")
    damage = position(body, "UCataclysmDetonation::DamageMultiplierFor(this)", "ExplodeSetOffBy")
    position(body, "ShareOfFigure", "ExplodeSetOffBy")
    area = position(body, "UCataclysmDetonation::AreaMultiplierFor(this)", "ExplodeSetOffBy")
    found = position(body, "FindEnemiesInSphere", "ExplodeSetOffBy")
    assert damage < found and found < area, "the area multiplier is not on the sphere's radius"


def test_made_room_detonates_a_released_minion_instead_of_destroying_it():
    body = body_of(COMMAND, "int32 UCataclysmCommand::ReleaseOldestForAThrall")
    detonates = position(body, "if (!UCataclysmDetonation::DetonateOnRelease(Cast<ACataclysmMinion>(Oldest)))",
                         "ReleaseOldestForAThrall")
    destroys = position(body, "Oldest->Destroy();", "ReleaseOldestForAThrall")
    assert detonates < destroys, "the release destroys the minion before asking whether it detonates"


def test_ripened_is_asked_after_rekindled_and_before_the_actor_goes():
    body = body_of(MINION, "void ACataclysmMinion::LifeSpanExpired")
    rekindled = position(body, "RenewLifetimeIfRekindled()", "LifeSpanExpired")
    ripened = position(body, "UCataclysmDetonation::RipenAtLifespanEnd(this)", "LifeSpanExpired")
    gone = position(body, "Super::LifeSpanExpired()", "LifeSpanExpired")
    assert rekindled < ripened < gone


def test_a_living_bombs_attack_is_its_detonation_before_any_blow():
    body = body_of(MINION, "void ACataclysmMinion::AttackTarget")
    contact = position(body, "UCataclysmDetonation::DetonateOnContact(this, Target)", "AttackTarget")
    bolted = position(body, "FireBoltedAttacksAt(Target)", "AttackTarget")
    assert contact < bolted
    reach = body_of(MINION, "float ACataclysmMinion::AttackReachCm")
    position(reach, "UCataclysmDetonation::AttackReachCmFor(this, ReachCm)", "AttackReachCm")


def test_a_detonation_is_told_first_and_counted_after_the_blast():
    """`Detonate` tells the summoner `NoteMinionDied` before the blast (Marrow Feast), and raises `minion_detonated`
    (Bursting Ranks) after it, so a detonation is raised by those before it and not by itself."""
    body = body_of(DETONATION, "bool UCataclysmDetonation::Detonate")
    told = position(body, "Told->NoteMinionDied(Minion, Tags)", "Detonate")
    blast = position(body, "Minion->ExplodeSetOffBy(Skill, SharePercent / 100.0f)", "Detonate")
    noted = position(body, "NoteDetonation(Summoner, Tags)", "Detonate")
    gain = position(body, "UCataclysmFervour::GainStated(Told, Reserved * Multiple)", "Detonate")
    ring = position(body, "Detonate(Each.Get(), Skill, SpreadPercent, /*bMaySpread=*/false)", "Detonate")
    assert told < blast < gain < noted < ring


def test_the_minion_asking_fills_its_age_and_its_fellows():
    body = body_of(SYSTEM, "float UCataclysmAbilitySystemComponent::MultiplierForStatAgainst")
    filled = position(body, "State.MinionSecondsActive = AskingMinion->GetGameTimeSinceCreation();",
                      "MultiplierForStatAgainst")
    fellows = position(body, "State.OtherMinionMetresFromAskingMinion.Add(", "MultiplierForStatAgainst")
    judged = position(body, "UCataclysmStatPipeline::Evaluate(", "MultiplierForStatAgainst")
    assert filled < judged and fellows < judged


def test_a_summon_rolls_one_more_and_the_horde_ascendant_answers_a_skill_use():
    summon = body_of(SKILL_TEMPLATES, "ACataclysmMinion* UCataclysmSummonSkill::SummonOne")
    guarded = position(summon, "if (!bExtraRaiseRolledThisUse && !bRaisingOneMore)", "SummonOne")
    rolled = position(summon, "RaiseOneMoreIfRolled(Minion);", "SummonOne")
    assert guarded < rolled, "Teeming Grave must roll once a use: the roll sits inside the once-a-use guard"
    activated = body_of(SKILL_TEMPLATES, "void UCataclysmSummonSkill::ActivateAbility")
    position(activated, "bExtraRaiseRolledThisUse = false;", "UCataclysmSummonSkill::ActivateAbility")
    used = body_of(PLAYER, "void ACataclysmPlayerCharacter::OnSkillWasUsed")
    asked = position(used, "UCataclysmHordeAscendant::Holds(this)", "OnSkillWasUsed")
    cast = position(used, "UCataclysmHordeAscendant::EveryFollowerNearCasts(this, Used, Aimed)", "OnSkillWasUsed")
    assert asked < cast
    assert "SetTimerForNextTick" in used[asked:cast], "the Horde Ascendant's casts are not made on the next tick"
