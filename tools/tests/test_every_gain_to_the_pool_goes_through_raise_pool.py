"""Every gain to the class resource pool goes through `UCataclysmFervour::RaisePool`.

WHY THIS EXISTS. `RaisePool` holds every gain to the full bar after reserves and hands what does not fit to the Soul
Collector's Overflow (since commit 61a254cbe), and a gain that writes the pool by itself skips both. The Death trees'
slice C4b1 (2026-11-25) made it the one way for Kindred Fires and Borrowed Time, which issue #2951 retired. There is no hook on the
attribute that could hear it instead: `UAbilitySystemComponent::ApplyModToAttribute` reaches the pool through
`FActiveGameplayEffectsContainer::ApplyModToAttribute`, which only calls `SetAttributeBaseValue`, so the attribute
set's `PostGameplayEffectExecute` never runs; and the hooks that do run also see `SetNumericAttributeBase`, which the
coordinating session ruled is no gain (read in the UE 5.8 source on 2026-11-25). So every gain calls the helper, and
this check is what keeps a new one from skipping it: the War session's Fervour sources and the Death skills' kill
Fervour add gain paths when they merge.

WHAT IS ASSERTED. Every `ApplyModToAttribute` call outside the tests, counted by file and the function it sits in, is
in `INVENTORY` with the reason it is not a gain to the pool, and nothing listed has gone; each function that gains
the pool calls `RaisePool`; and a kill's payment is marked. A new call fails until it is listed, which is the point: listing it means saying why it is
not a gain. Read from the source with comments and strings removed, because continuous integration compiles no C++.
"""

from __future__ import annotations

import collections
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "game" / "Source"

#: A function definition at the start of a line: a return type, then `Class::Name(`.
DEFINITION = re.compile(r"^[A-Za-z_][\w:<>,\*&\s]*?\b((?:[A-Za-z_]\w*::)+~?[A-Za-z_]\w*)\s*\(")
CALL = re.compile(r"\bApplyModToAttribute\s*\(")

FERVOUR = "game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp"
ASC = "game/Source/Cataclysm/AbilitySystem/CataclysmAbilitySystemComponent.cpp"
WAR = "game/Source/Cataclysm/AbilitySystem/CataclysmWarFervour.cpp"

#: Every `ApplyModToAttribute` call outside the tests: (file, function) -> (how many, why it is not a gain to the pool).
INVENTORY: dict[tuple[str, str], tuple[int, str]] = {
    (FERVOUR, "UCataclysmFervour::RaisePool"): (1, "the helper itself: the one positive write to the pool"),
    (FERVOUR, "UCataclysmFervour::Move"): (1, "the loss branch only; its gain branch calls RaisePool"),
    (FERVOUR, "UCataclysmFervour::DecayStep"): (1, "the Ravager's decay out of contact, a negative change"),
    (FERVOUR, "UCataclysmFervour::KillDecayStep"): (1, "the Death classes' decay after a kill, a negative change"),
    (FERVOUR, "UCataclysmFervour::RestoreHealthOnKill"): (1, "Wrung Out's spend of Fervour, a negative change"),
    (FERVOUR, "UCataclysmFervour::BuyDamageForEnemiesStruckTogether"): (1, "a spend of Fervour, a negative change"),
    (FERVOUR, "UCataclysmFervour::SpendAll"): (1, "Requiem's spend of every Fervour held, a negative change"),
    (ASC, "UCataclysmAbilitySystemComponent::TakeFervourForLethalSave"): (1, "Phylactery's spend, a negative change"),
    (ASC, "UCataclysmAbilitySystemComponent::ApplyPoolAction"): (
        1, "a row's drain of any pool, floored, a negative change; its gain branch goes through TopUp"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmRegeneration.cpp", "UCataclysmRegeneration::TopUp"): (
        1, "a top-up of a pool other than the class resource; the class resource goes through RaisePool"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmGameplayAbility.cpp", "UCataclysmGameplayAbility::ApplyCost"): (
        2, "a skill's Fervour cost and its charged Fervour, both negative changes"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmGameplayAbility.cpp", "UCataclysmGameplayAbility::ApplyCooldown"): (
        1, "Fervour borrowed for a cooldown, a negative change"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp", "UCataclysmSkillTemplate::SpendSoulFuel"): (
        1, "Soul Fuel's spend on a skill's use, a negative change (the Death finish slice A, 2026-11-26)"),
    # UCataclysmAuraSkill::Pulse left this list on 2026-11-27: an aura's pulse charges nothing since auras began to
    # reserve mana rather than drain it.
    ("game/Source/Cataclysm/AbilitySystem/CataclysmDelayedDamage.cpp", "UCataclysmDelayedDamage::DrainStep"): (
        1, "health, not the pool"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmHealthDebt.cpp", "UCataclysmHealthDebt::Defer"): (
        1, "the health owed, not the pool"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmHealthDebt.cpp", "UCataclysmHealthDebt::DrainIfDue"): (
        1, "health, not the pool"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmHealthDebt.cpp",
     "UCataclysmHealthDebt::DrainWhileDebtExceedsHealth"): (1, "health, not the pool"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmMinion.cpp", "ACataclysmMinion::StepNoDamageAndDrain"): (
        1, "a minion's health, not the pool"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp", "UCataclysmSkillTemplate::PayHealthCost"): (
        1, "health, not the pool"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp", "UCataclysmSkillTemplate::PayManaOnHitTo"): (
        1, "mana, not the pool"),
    ("game/Source/Cataclysm/Dungeon/CataclysmDungeonGameMode.cpp", "ACataclysmDungeonGameMode::StepSufferingAura"): (
        1, "mana, not the pool"),
    # THE WAR LINE'S, since the Death trees' bundle 3 merge (2026-11-25, obligations 14 and 17). Its two gains,
    # `GainByRate` and Cleave's net gain, go through RaisePool (in GAINS below); these are its losses.
    (WAR, "FCataclysmWarFervour::TakeFromPool"): (1, "a War spend of Fervour, a negative change"),
    (WAR, "FCataclysmWarFervour::RetaliationPulseDrainStep"): (1, "Vengeful Discharge's drain, a negative change"),
    (WAR, "FCataclysmWarFervour::TradeForEnemiesStruckTogether"): (
        1, "Cleave's net loss only; its net gain calls RaisePool"),
    (WAR, "FCataclysmWarFervour::OutOfCombatDecayStep"): (1, "the War classes' decay out of combat, a negative change"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmBerserkerStates.cpp", "FCataclysmBerserkerStates::DrainStep"): (1, "Wrath's and Frenzy's drain, a negative change"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmBerserkerStates.cpp", "FCataclysmBerserkerStates::SpendBerserking"): (1, "Berserking's spend of Fervour, a negative change"),
}

#: Every function that gains the pool, each of which has to call `RaisePool`.
GAINS = (
    (FERVOUR, "UCataclysmFervour::Move"),
    (FERVOUR, "UCataclysmFervour::GainPerSecondStep"),
    (FERVOUR, "UCataclysmFervour::GainForCast"),
    (FERVOUR, "UCataclysmFervour::GainOnDroppingLow"),
    (FERVOUR, "UCataclysmFervour::GainForEnemiesHit"),
    (FERVOUR, "UCataclysmFervour::GainOnMinionDeath"),
    (FERVOUR, "UCataclysmFervour::GainOnEnemyDeathNearby"),
    (FERVOUR, "UCataclysmFervour::GainOnOwnKill"),
    # AND A SKILL ROW'S STATED FERVOUR (the Death skills' `KillFervour` and `HitFervour`), since the Death skills merge.
    (FERVOUR, "UCataclysmFervour::GainStated"),
    (ASC, "UCataclysmAbilitySystemComponent::ReturnFervourDue"),
    ("game/Source/Cataclysm/AbilitySystem/CataclysmRegeneration.cpp", "UCataclysmRegeneration::TopUp"),
    # AND THE WAR LINE'S TWO, since the bundle 3 merge: every rate (`GainByRate`) and Cleave's net gain.
    (WAR, "FCataclysmWarFervour::GainByRate"),
    (WAR, "FCataclysmWarFervour::TradeForEnemiesStruckTogether"),
)


def code_only(text: str) -> str:
    """Comments and string contents removed, keeping every line where it was."""
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', text)


def bodies() -> dict[tuple[str, str], str]:
    """The code of each function in every .cpp outside the tests, keyed by (file, Class::Name)."""
    found: dict[tuple[str, str], list[str]] = collections.defaultdict(list)
    for path in sorted(SOURCE.rglob("*.cpp")):
        relative = path.relative_to(ROOT).as_posix()
        if "/Tests/" in relative:
            continue
        function = ""
        for line in code_only(path.read_text(encoding="utf-8", errors="replace")).splitlines():
            match = DEFINITION.match(line)
            if match:
                function = match.group(1)
            found[(relative, function)].append(line)
    return {key: "\n".join(lines) for key, lines in found.items()}


def call_sites() -> collections.Counter:
    return collections.Counter({key: len(CALL.findall(body)) for key, body in bodies().items()
                                if CALL.search(body)})


def test_every_call_site_is_in_the_inventory():
    found = call_sites()
    unlisted = sorted((key, count) for key, count in found.items()
                      if key not in INVENTORY or INVENTORY[key][0] != count)
    assert not unlisted, (
        "these ApplyModToAttribute calls are not in INVENTORY, or not as many as it says:\n"
        + "\n".join(f"  {path}  {function}: {count}" for (path, function), count in unlisted)
        + "\n\nA positive change to the class resource pool must go through UCataclysmFervour::RaisePool, or "
          "Overflow never hears of it. Anything else: list it with why it is not a gain. "
          f"Scope: {sum(found.values())} calls in {len(found)} functions, every .cpp under game/Source but the tests.")


def test_the_inventory_holds_no_call_site_that_has_gone():
    found = call_sites()
    gone = sorted(key for key in INVENTORY if key not in found)
    assert not gone, f"listed in INVENTORY and no longer in the source: {gone}"


def test_every_gain_to_the_pool_calls_the_helper():
    every = bodies()
    missing = [key for key in GAINS if key not in every]
    assert not missing, f"these gain functions were not found by name: {missing}"
    skipping = [key for key in GAINS if "RaisePool(" not in every[key]]
    assert not skipping, f"these functions gain the pool without UCataclysmFervour::RaisePool: {skipping}"


def test_the_kill_payment_is_marked_for_its_whole_length():
    """The marker GainOnOwnKill opens. Any Fuel and Borrowed Time, which read it, were retired by issue #2951; the
    marker stays until it is removed on its own."""
    body = bodies()[(FERVOUR, "UCataclysmFervour::GainOnOwnKill")]
    opened = body.find("BeginKillPayment()")
    closed = body.find("EndKillPayment()")
    paid = body.find("RaisePool(")
    assert 0 <= opened < paid and 0 <= closed, (
        "GainOnOwnKill must open the kill-payment marker before it pays and close it on the way out")
    assert re.search(r"ON_SCOPE_EXIT\s*\{\s*Paying->EndKillPayment\(\);\s*\}", body), (
        "the marker must be closed on every way out of GainOnOwnKill, by ON_SCOPE_EXIT")


#: Every call that pays a skill row's `KillFervour`, by file: each must pass `bForAKill` true. The Death skills merge,
#: obligation 4: "A skill's kill Fervour sets the kill-payment marker and goes through RaisePool."
KILL_FERVOUR_PAYERS = {
    "game/Source/Cataclysm/AbilitySystem/CataclysmKillFervour.cpp": 1,
    "game/Source/Cataclysm/AbilitySystem/CataclysmBuriedWeapon.cpp": 2,
    "game/Source/Cataclysm/AbilitySystem/CataclysmCurseSpread.cpp": 1,
    "game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplates.cpp": 1,
}


def test_a_skills_stated_fervour_marks_a_kills_payment_before_it_is_scaled():
    """GainStated opens the marker before the rate is asked, closes it on every way out, and pays through RaisePool."""
    body = bodies()[(FERVOUR, "UCataclysmFervour::GainStated")]
    opened = body.find("BeginKillPayment()")
    scaled = body.find("ClassResourceGainScaled(")
    paid = body.find("RaisePool(")
    assert 0 <= opened < min(scaled, paid), (
        "GainStated must open the kill-payment marker before ClassResourceGainScaled asks the rate, and pay through "
        "RaisePool")
    assert re.search(r"ON_SCOPE_EXIT\s*\{[^}]*Paying->EndKillPayment\(\);", body), (
        "the marker must be closed on every way out of GainStated, by ON_SCOPE_EXIT")
    assert "bForAKill" in body[:opened], "the marker opens only for a kill's payment"


def test_every_kill_fervour_payment_is_marked_and_a_hit_fervour_payment_is_not():
    found = {}
    for path in KILL_FERVOUR_PAYERS:
        text = code_only((ROOT / path).read_text(encoding="utf-8"))
        calls = re.findall(r"GainStated\((.*?)\);", text, flags=re.S)
        found[path] = [c for c in calls if "KillFervour" in c or "PaysOnDeath" in c]
        assert len(found[path]) == KILL_FERVOUR_PAYERS[path], (
            f"{path}: {len(found[path])} KillFervour payments found, {KILL_FERVOUR_PAYERS[path]} listed")
        unmarked = [c for c in found[path] if "true" not in c]
        assert not unmarked, f"{path}: a KillFervour payment without bForAKill true: {unmarked}"
    hit = code_only((ROOT / "game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp").read_text(encoding="utf-8"))
    hit_calls = re.findall(r"GainStated\((.*?)\);", hit, flags=re.S)
    assert hit_calls and all("true" not in c for c in hit_calls), (
        "a row's HitFervour pays for a hit, not a death, so it must not open the kill-payment marker")

