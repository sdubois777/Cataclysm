"""The dungeon modifier rules built in C++ name real rows, and their rows still
say what the rules take.

WHY THIS EXISTS. Issue #41. `game/Source/Cataclysm/Dungeon/
CataclysmDungeonModifierEffects.h` and `.cpp` give a few of the 117 rows of
`game/Data/DungeonModifiers.csv` a rule: Starvation takes 1% of maximum health
and energy shield a floor up to 60%, Dehydration 1% of maximum mana a floor,
Forced March a share of maximum health a second from a player standing still,
The Nihil's Embrace a point of every resistance for each stretch walked,
Death's Embrace a share of every amount of healing for each stretch of time the
player stays on the floor, the Field Medic heals other enemies and does not
attack, Unstable Dimensions draws another modifier onto the floor, Infernal
Rain drops patches of burning ground, Mortal Decay saps health faster the
deeper the floor is until the player reaps something, and Wasting Sickness
stacks a reduction to both of the player's maximums when an enemy's blow lands,
Grasping Tentacles grabs a player who lingers within one's reach and lets go
again, and the Edict of Silence locks every skill but the basic attack for
fifteen seconds in every ninety.

THAT LIST IS NOT A COUNT, AND IT USED TO BE ONE. This paragraph said "five of the
117 rows" and named five; three rules had been added since without it moving, so
it was wrong before anybody read it. `UCataclysmDungeonModifierEffects::
KeysWithARule` is the list two automation tests actually hold to the table, and
`test_every_row_key_the_rules_name_is_a_row_of_the_table` below needs no count at
all -- it reads whatever keys the source names.
The C++ automation tests prove the rules do that, and they build every number
they check by hand, so all of them would keep passing through two changes that
break the game:

  - the row key the rule looks for is renamed in the design workbook, after
    which no floor ever carries it and the rule never fires;
  - the row's description changes its number, after which the game takes one
    share and the floor panel shows the player another.

This file runs on every pull request as part of `python -m pytest`. The C++
tests run only when somebody runs `python tools/unreal_build.py tests`.
"""

from __future__ import annotations

import csv
import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TABLE = REPO_ROOT / "game" / "Data" / "DungeonModifiers.csv"
EFFECTS_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
EFFECTS_HEADER = EFFECTS_DIR / "CataclysmDungeonModifierEffects.h"
EFFECTS_SOURCE = EFFECTS_DIR / "CataclysmDungeonModifierEffects.cpp"

#: A dungeon modifier row key written as a C++ string literal. The first word is
#: one of the eight Cataclysms or Generic, which is how every key in the table
#: begins.
ROW_KEY = re.compile(
    r'TEXT\("((?:Celestial|Chaos|Death|Demonic|Famine|Pestilence|Void|War|Generic)'
    r'_\w+)"\)')


def rows() -> dict[str, dict[str, str]]:
    with TABLE.open(newline="", encoding="utf-8") as handle:
        return {row["Name"]: row for row in csv.DictReader(handle)}


def flat(text: str) -> str:
    """The text with every run of whitespace made one space."""
    return " ".join(text.split())


def constant(name: str, header: str = "") -> float:
    """A `static constexpr float` by name, from the effects header by default.

    THE HEADER IS A PARAMETER BECAUSE ONE RULE IS DERIVED FROM A FIGURE THAT IS
    NOT ITS OWN. The Artillery Strike's warning is twice the time it takes to
    walk out of its circle, and the walk speed belongs to the player character,
    not to this table. Reading it from where it is defined is what stops the two
    drifting apart; writing it down here would be a copy that goes stale.
    """
    where = REPO_ROOT / header if header else EFFECTS_HEADER
    text = where.read_text(encoding="utf-8")
    found = re.search(rf"\b{re.escape(name)}\s*=\s*([0-9]+(?:\.[0-9]+)?)f\s*;", text)
    assert found, f"{name} is not declared in {where.name}"
    return float(found.group(1))


def test_the_key_search_finds_keys_in_text_built_to_hold_them():
    """A positive control: the search this file relies on is not blind.

    WRITTEN OUT HERE RATHER THAN READ FROM THE C++, because a search that is
    silently broken finds nothing in the real file too, and "nothing named" would
    then read as "nothing wrong".
    """
    sample = ('x = TEXT("Famine_Starvation"); y = TEXT("Chaos_Unstable_Dimensions");'
              ' z = TEXT("max_health");')
    assert ROW_KEY.findall(sample) == ["Famine_Starvation", "Chaos_Unstable_Dimensions"]


def test_every_row_key_the_rules_name_is_a_row_of_the_table():
    named = ROW_KEY.findall(EFFECTS_SOURCE.read_text(encoding="utf-8"))
    assert named, f"no dungeon modifier row key found in {EFFECTS_SOURCE.name}"

    missing = sorted(set(named) - set(rows()))
    assert not missing, (
        f"{EFFECTS_SOURCE.name} names {missing}, which "
        f"{TABLE.relative_to(REPO_ROOT).as_posix()} does not hold. A rule keyed "
        f"by a name that is not a row never fires.")


def test_starvation_still_says_what_the_code_takes():
    words = flat(rows()["Famine_Starvation"]["Description"])
    per_floor = constant("StarvationPercentPerFloor")
    most = constant("StarvationMostPercent")

    assert f"reduced by {per_floor:g}%" in words, words
    assert f"Up to {most:g}%" in words, words


def test_dehydration_still_says_what_the_code_takes():
    words = flat(rows()["Famine_Dehydration"]["Description"])
    per_floor = constant("DehydrationPercentPerFloor")

    assert f"reduced by {per_floor:g}%" in words, words


def test_dehydrations_cap_is_still_a_judgement_and_not_the_rows():
    """Dehydration stops at 60% because Starvation does, and its row says nothing.

    IF THE ROW EVER STATES A CAP, this fails, so the C++ constant is checked
    against it and `docs/DECISIONS.md` stops calling it a judgement.
    """
    words = flat(rows()["Famine_Dehydration"]["Description"]).lower()
    assert "up to" not in words, (
        "The Dehydration row now states a cap. Check "
        "DehydrationMostPercent against it and update docs/DECISIONS.md.")


def test_forced_march_still_says_its_three_second_threshold():
    """The only number either new row states, and the rule reads it."""
    words = flat(rows()["War_Forced_March"]["Description"])
    seconds = constant("ForcedMarchSecondsBeforeDamage")

    assert f">{seconds:g}s" in words, words
    assert "stacking damage" in words, words


def test_forced_marchs_size_rate_and_cap_are_judgements_not_the_rows():
    """Its row says when the damage starts and nothing about how big it is.

    IF THE ROW EVER STATES A SHARE, A RATE OR A CAP, this fails, so the two
    C++ constants are checked against it and `docs/DECISIONS.md` stops
    calling them judgements.
    """
    words = flat(rows()["War_Forced_March"]["Description"])

    assert "%" not in words, (
        "The Forced March row now states a percentage. Check "
        "ForcedMarchPercentPerStackPerSecond against it and update "
        "docs/DECISIONS.md.")
    assert [c for c in words if c.isdigit()] == ["3"], (
        "The Forced March row now states a number besides its three-second "
        "threshold. Check ForcedMarchPercentPerStackPerSecond and "
        "ForcedMarchMostStacks against it and update docs/DECISIONS.md.")


def test_the_nihils_embrace_states_no_number_of_its_own():
    """All four of its constants are judgements: its row states no number.

    IF THE ROW EVER STATES ONE, this fails, so the C++ constants are checked
    against it and `docs/DECISIONS.md` stops calling them judgements.
    """
    words = flat(rows()["Void_The_Nihil_s_Embrace"]["Description"])

    assert "%" not in words, words
    assert not [c for c in words if c.isdigit()], (
        "The Nihil's Embrace row now states a number. Check "
        "NihilsEmbraceMetresPerResistancePercent, "
        "NihilsEmbraceMostResistancePercent, "
        "NihilsEmbraceRewardResistancePercent and "
        "NihilsEmbraceRewardSeconds against it and update "
        "docs/DECISIONS.md.")


def test_the_nihils_embrace_says_permanent_and_asks_for_a_boss():
    """Two wordings the rule rests on: a permanent loss, a boss that ends it.

    THE ROW'S OWN WORDS ARE THE EVIDENCE. "permanently" is why the distance
    walked is not forgotten when the player takes the stairs, and "high tier
    enemy" with "boss" is why the cleanse uses the game's own boss line.

    IT NAMES NO RUNG OF THE RARITY LADDER, which is what keeps the exact
    line a judgement: a Herald is a mini-boss the player meets often and
    deliberately does not satisfy the cleanse.
    """
    words = flat(rows()["Void_The_Nihil_s_Embrace"]["Description"]).lower()

    assert "permanently reduced" in words, words
    assert "high tier enemy" in words, words
    assert "boss" in words, words

    named = [rung for rung in ("common", "elite", "legendary", "herald")
             if rung in words]
    assert not named, (
        f"The Nihil's Embrace row now names {named}. The cleanse's boss "
        "threshold is no longer a judgement; check it against the row and "
        "update docs/DECISIONS.md.")


def test_singularity_wells_still_says_it_slows_by_forty():
    """The one number its row states, and the rule uses it as stated.

    `SingularityWellsSlowPercent` is not a judgement for that reason, and it is the
    only one of the row's six constants that is not.
    """
    words = flat(rows()["Void_Singularity_Wells"]["Description"])
    percent = constant("SingularityWellsSlowPercent")

    assert f"slowing movement by {percent:g}%" in words, words


def test_singularity_wells_other_five_figures_are_judgements():
    """Its row states the 40% and nothing else about the wells.

    FIVE OF ITS SIX CONSTANTS ARE NOT THE ROW'S: how wide a well is, how far from
    the player it appears, how many burn at once, how much of the player's maximum
    health a second inside one costs, and how often another appears.
    `docs/DECISIONS.md` records each. IF THE ROW EVER STATES ONE, this fails, so the
    constant is checked against it and the log stops calling it a judgement.
    """
    words = flat(rows()["Void_Singularity_Wells"]["Description"])

    assert [c for c in words if c.isdigit()] == ["4", "0"], (
        "The Singularity Wells row now states a number besides its 40%. Check "
        "SingularityWellsRadiusCm, SingularityWellsFallsWithinCm, "
        "SingularityWellsMostWells, SingularityWellsPercentPerSecond and "
        "SingularityWellsSecondsBetweenWells against it and update "
        "docs/DECISIONS.md.")


def test_withered_ground_still_says_it_takes_eighty_per_cent():
    """The one number its row states, and the rule uses it as stated.

    `WitheredGroundRecoveryLessPercent` is not a judgement for that reason. It is
    the only one of this rule's two constants that came with the row; the patch
    radius did not.
    """
    words = flat(rows()["Famine_Withered_Ground"]["Description"])
    percent = constant("WitheredGroundRecoveryLessPercent")

    assert f"is reduced by {percent:g}%" in words, words


def test_withered_ground_still_names_all_four_kinds_of_recovery():
    """The row asks for four stats and the rule writes four Less multipliers.

    WHY THIS IS WORTH PINNING. Two of the four are worth nothing to almost every
    character today -- `game/Data/ClassStats.csv` gives `life_leech` to one class
    and gives no class any `mana_leech`, and a multiplier on a base of zero is
    zero. So somebody tidying up could remove the two leech modifiers, see no test
    fail and no behaviour change, and quietly make the rule stop matching its row.
    This is what would notice.
    """
    words = flat(rows()["Famine_Withered_Ground"]["Description"])
    source = (EFFECTS_DIR / "CataclysmDungeonModifierEffects.cpp").read_text(
        encoding="utf-8")

    assert "Health and Mana recovery (regen/leech)" in words, words
    for stat in ("health_regen", "mana_regen", "life_leech", "mana_leech"):
        assert f'TEXT("{stat}")' in source, (
            f"the Withered Ground row says 'Health and Mana recovery "
            f"(regen/leech)' and {stat!r} is no longer named in "
            "CataclysmDungeonModifierEffects.cpp. Four stats are what that phrase "
            "means: health and mana regeneration, and life and mana leech.")


def test_withered_grounds_patch_radius_is_a_judgement_not_the_rows():
    """Its row states the 80% and nothing else.

    The patch radius is not the row's. `docs/DECISIONS.md` records it. IF THE ROW
    EVER STATES ONE, this fails, so the constant is checked against it and the log
    stops calling it a judgement.
    """
    words = flat(rows()["Famine_Withered_Ground"]["Description"])

    assert [c for c in words if c.isdigit()] == ["8", "0"], (
        "The Withered Ground row now states a number besides its 80%. Check "
        "WitheredGroundPatchRadiusCm against it and update docs/DECISIONS.md.")


def test_singularity_wells_damage_is_void_and_comes_from_its_own_column():
    """Why the wells are typed without any type being named in code.

    The row says "dealing void damage", and its `CataclysmType` column says Void, so
    the rule reads the column rather than naming a type. That is what lets a row
    retyped in the design workbook retype its wells with no code change — and it is
    what stops the damage meeting none of the player's eight resistances, which is
    what untyped hazard damage did before issue #1605.
    """
    row = rows()["Void_Singularity_Wells"]
    words = flat(row["Description"]).lower()

    assert "void damage" in words, words
    assert row["CataclysmType"] == "Void", row["CataclysmType"]


def test_singularity_wells_still_asks_for_a_pull_it_does_not_have():
    """The row asks for a pull, and that is why it is only partly built.

    THIS TEST IS A REMINDER, NOT A GUARD, and says so. The pull is unbuilt:
    `UCataclysmSkillEffects::ApplyPull` cannot be used on a repeating beat because
    the diminishing-returns rule halves every displacement inside a five second
    window, and a projectile's direction is private and fixed at launch.

    IF THE ROW EVER STOPS ASKING FOR A PULL, this fails — and the partly-built state
    in `UCataclysmDungeonModifierEffects::BuiltStateOf` has to be revisited, because
    the thing it is waiting for would no longer be wanted.
    """
    words = flat(rows()["Void_Singularity_Wells"]["Description"]).lower()

    assert "pull" in words, (
        "The Singularity Wells row no longer asks for a pull. Its built state is "
        "Partly because the pull is missing; re-read BuiltStateOf.")
    assert "projectiles" in words, (
        "The Singularity Wells row no longer mentions projectiles. The pull has two "
        "halves and this was the second; re-read BuiltStateOf.")


def test_infernal_rain_still_says_its_patches_last_ten_seconds():
    """The one number its row states, and the rule reads it.

    `InfernalRainPatchSeconds` is not a judgement for that reason, and it is the
    only one of Infernal Rain's six constants that is not.
    """
    words = flat(rows()["Demonic_Infernal_Rain"]["Description"])
    seconds = constant("InfernalRainPatchSeconds")

    assert f"for {seconds:g} seconds" in words, words
    assert "patches of burning ground" in words, words


def test_infernal_rains_cadence_cap_share_and_reach_are_judgements():
    """Its row states the ten seconds and nothing else about the rain.

    FIVE OF ITS SIX CONSTANTS ARE JUDGEMENTS: how often a patch falls, how many
    burn at once, how wide one is, how much of the player's maximum health a
    second in one costs, and how far from the player they land.
    `docs/DECISIONS.md` records each. IF THE ROW EVER STATES ONE, this fails, so
    the constant is checked against it and the log stops calling it a judgement.
    """
    words = flat(rows()["Demonic_Infernal_Rain"]["Description"])

    assert "%" not in words, (
        "The Infernal Rain row now states a percentage. Check "
        "InfernalRainPercentPerSecond against it and update docs/DECISIONS.md.")
    assert [c for c in words if c.isdigit()] == ["1", "0"], (
        "The Infernal Rain row now states a number besides its ten seconds. "
        "Check InfernalRainSecondsBetweenPatches, InfernalRainMostPatches, "
        "InfernalRainRadiusCm, InfernalRainPercentPerSecond and "
        "InfernalRainFallsWithinCm against it and update docs/DECISIONS.md.")


def test_infernal_rain_asks_for_ground_rather_than_a_hit():
    """Why the rule lays a timed patch instead of damaging the player directly.

    "leaving patches of burning ground" is the wording the whole shape rests on:
    ground is a thing to walk out of, so the patch is placed away from the
    player's feet and expires, rather than being an unavoidable hit. The C++ test
    `AFloorCarryingInfernalRainDropsTypedPatches` asserts both of those against
    the patch it produces.

    AND "FIRE DAMAGE" IS THE ROW'S OWN CATACLYSM TYPE. There is no Fire among the
    eight resistances; the row's `CataclysmType` column says Demonic, and the
    rule reads that column rather than naming a type itself.
    """
    row = rows()["Demonic_Infernal_Rain"]
    words = flat(row["Description"]).lower()

    assert "patches of burning ground" in words, words
    assert "over time" in words, words
    assert row["CataclysmType"] == "Demonic", row["CataclysmType"]


def test_infernal_rain_names_combat_zones_which_the_game_has_no_concept_of():
    """The reading "near the player" is a judgement, and this is what dates it.

    THE ROW SAYS "in combat zones". Nothing in the game tracks where fighting is
    happening: `ECataclysmFloorLayout::Arena` is a whole floor's shape rather
    than a region inside one. So the rule drops patches near the player, which
    `docs/DECISIONS.md` records as a judgement.

    THIS TEST IS A REMINDER AND NOT A GUARD, and says so plainly: it fails if the
    row stops saying "combat zones", which is the moment to re-read the
    judgement. It cannot notice the game GAINING a combat-zone concept, which is
    the other thing that would retire the judgement.
    """
    words = flat(rows()["Demonic_Infernal_Rain"]["Description"]).lower()

    assert "combat zones" in words, (
        "The Infernal Rain row no longer says 'combat zones'. Re-read the "
        "judgement in docs/DECISIONS.md that reads it as 'near the player'.")


def test_deaths_embrace_states_no_number_of_its_own():
    """All three of its constants are judgements: its row states no number.

    IF THE ROW EVER STATES ONE, this fails, so the C++ constants are checked
    against it and `docs/DECISIONS.md` stops calling them judgements.
    """
    words = flat(rows()["Death_Death_s_Embrace"]["Description"])

    assert "%" not in words, words
    assert not [c for c in words if c.isdigit()], (
        "The Death's Embrace row now states a number. Check "
        "DeathsEmbracePercentPerStack, DeathsEmbraceMostStacks and "
        "DeathsEmbraceSecondsPerStack against it and update "
        "docs/DECISIONS.md.")


def test_deaths_embrace_says_it_stacks_cuts_healing_and_resets_on_a_floor():
    """Three wordings the rule rests on, one of them load-bearing.

    THE RESET IS THE ONE PART THIS ROW ASKS FOR RATHER THAN THE CODE NEEDING.
    Starvation's per-floor share and Forced March's clearing both fall out of
    what they are counted from; "Stacks reset when entering a new floor" is a
    promise in the data, and `ACataclysmDungeonGameMode::ApplyFloorRulesToPlayer`
    puts `DeathsEmbraceSecondsOnFloor` and `DeathsEmbraceStacksApplied` back to
    nothing to keep it. If the row stops saying this, that reset is wrong.

    "HEALING RECEIVED" IS WHY THE STAT IS AN AMOUNT AND NOT A RATE. The row does
    not say "regeneration" or "recovery", which Withered Ground and the
    regeneration enchantment rows do, so this one reduces every route that
    restores health rather than the rates alone.
    """
    words = flat(rows()["Death_Death_s_Embrace"]["Description"]).lower()

    assert "gain stacks" in words, words
    assert "reduces healing received" in words, words
    assert "reset when entering a new floor" in words, words

    rate = [word for word in ("regeneration", "regen", "leech", "recovery")
            if word in words]
    assert not rate, (
        f"The Death's Embrace row now names {rate}. It reduces the AMOUNT of "
        "healing through healing_received_reduction, and a row naming a rate "
        "wants a Less multiplier on health_regen instead; check the rule "
        "against the row and update docs/DECISIONS.md.")


def test_mortal_decay_states_no_number_of_its_own():
    """All four of its constants are judgements: its row states no number.

    IF THE ROW EVER STATES ONE, this fails, so the C++ constants are checked
    against it and `docs/DECISIONS.md` stops calling them judgements.
    """
    words = flat(rows()["Death_Mortal_Decay"]["Description"])

    assert "%" not in words, words
    assert not [c for c in words if c.isdigit()], (
        "The Mortal Decay row now states a number. Check "
        "MortalDecayPercentPerSecondPerFloor, MortalDecayMostPercentPerSecond, "
        "MortalDecaySlowPercent and MortalDecaySlowSeconds against it and "
        "update docs/DECISIONS.md.")


def test_mortal_decay_says_it_grows_with_progress_and_that_reaping_slows_it():
    """Three wordings the rule rests on, and the one that is load-bearing.

    "AS THEY PROGRESS THROUGH THE DUNGEON" IS THE FLOOR NUMBER, which is a
    reading of a standing rule in `CLAUDE.md` rather than a judgement: depth and
    reward are the same axis and depth and time are not, so a rule keyed to the
    walk would take less from a player who had bought faster walking at the same
    depth. `ACataclysmDungeonGameMode::StepMortalDecay` reads
    `FCataclysmFloorBrief::FloorNumber` for that reason. If the row stops saying
    this, that reading is wrong.

    "REAPING ENEMIES" NAMES WHO DOES THE KILLING, which is the one place this
    listener differs from Withered Ground's: that row says "Enemies leave
    patches ... on death" and takes every death, and this one requires the
    player to have dealt the killing blow.

    "TEMPORARILY SLOW" IS WHY A KILL CANNOT BUY IMMUNITY. The slow is a share
    below 100, and `MortalDecaySlowPercent` carries a static assertion saying so.
    """
    words = flat(rows()["Death_Mortal_Decay"]["Description"]).lower()

    assert "as they progress through the dungeon" in words, words
    assert "reaping enemies" in words, words
    assert "temporarily slow" in words, words

    walk = [word for word in ("walk", "walking", "metres", "meters", "distance",
                              "seconds", "minute")
            if word in words]
    assert not walk, (
        f"The Mortal Decay row now names {walk}. Its rate is keyed to the floor "
        "number, which was a reading of CLAUDE.md's depth-versus-time rule and "
        "not a judgement; re-read it against the row and update "
        "docs/DECISIONS.md.")


def test_mortal_decays_constants_still_describe_a_gradual_decay_a_kill_slows():
    """The two relationships the rule's shape depends on, checked where CI looks.

    WHY THIS DUPLICATES TWO `static_assert`s. Both live in
    `CataclysmDungeonModifierEffects.h` and both are compile-time, so the only
    thing that can fire them is a C++ build — and the build job is not a required
    check on a pull request. The fast suite is. So a change to these four
    constants that breaks the rule's shape would merge on a green tick with the
    assertion never evaluated; this is what stops that.

    IT DOES NOT DUPLICATE THE THIRD. The assertion tying the ceiling to
    `SingularityWellsPercentPerSecond` is about how this row compares to another
    rule's, which is an argument rather than a shape, and it is stated beside the
    constant where somebody changing it will read it.
    """
    per_floor = constant("MortalDecayPercentPerSecondPerFloor")
    most = constant("MortalDecayMostPercentPerSecond")
    slow = constant("MortalDecaySlowPercent")

    assert per_floor < most, (
        f"Mortal Decay reaches its ceiling on floor 1 ({per_floor} a floor "
        f"against a ceiling of {most}), so 'gradually ... as they progress "
        "through the dungeon' would describe nothing a player could observe.")
    assert 0.0 < slow < 100.0, (
        f"Mortal Decay's kill reward is {slow}%. At 100 a kill stops the "
        "affliction outright, which the row does not ask for -- it says "
        "'temporarily slow the effect' -- and at 0 the row's second sentence "
        "does nothing.")


def test_wasting_sickness_states_no_number_of_its_own():
    """All three of its constants are judgements: its row states no number.

    IF THE ROW EVER STATES ONE, this fails, so the C++ constants are checked
    against it and `docs/DECISIONS.md` stops calling them judgements.
    """
    words = flat(rows()["Famine_Wasting_Sickness"]["Description"])

    assert "%" not in words, words
    assert not [c for c in words if c.isdigit()], (
        "The Wasting Sickness row now states a number. Check "
        "WastingSicknessChancePercentPerHit, WastingSicknessPercentPerStack and "
        "WastingSicknessMostStacks against it and update docs/DECISIONS.md.")


def test_wasting_sickness_says_chance_stacking_both_maximums_and_its_two_cures():
    """Five wordings the rule rests on, two of them load-bearing.

    "A CHANCE" IS WHY A BLOW ROLLS. The rule compares a roll against
    `WastingSicknessChancePercentPerHit` rather than inflicting a stack on every
    blow, and a static assertion keeps that chance between 0 and 100.

    "STACKING" IS WHY THERE IS A COUNT AT ALL. This project's one-stack rule is
    about effects the PLAYER applies to enemies; a debuff enemies stack on the
    player sits outside it, which issue #913 records.

    "MAX HP AND MAX MANA" IS ONE FIGURE FOR TWO STATS, so the rule holds one
    per-stack share and `StatModifiersFor` writes two Less multipliers.

    "PERMANENT FOR THE DURATION OF THE DUNGEON" IS WHY THE STAIRS DO NOT CURE IT.
    `ACataclysmDungeonGameMode::ApplyFloorRulesToPlayer` puts the APPLIED figure
    back to nothing on a floor change and deliberately leaves the stack count
    alone. If the row stops saying this, that is wrong.

    "A FLOOR BOSS" IS THE ROW'S OWN CURE, and the reading that any boss on the
    floor satisfies it is a judgement recorded in `docs/DECISIONS.md`. The row's
    other cure -- the player's own death -- is NOT in the row: it comes from the
    owner's ruling of 2026-09-10, so nothing here can check it.
    """
    words = flat(rows()["Famine_Wasting_Sickness"]["Description"]).lower()

    assert "chance to inflict" in words, words
    assert "stacking debuff" in words, words
    assert "max hp and max mana" in words, words
    assert "permanent for the duration of the dungeon" in words, words
    assert "floor boss" in words, words


def test_wasting_sickness_names_no_rung_of_the_rarity_ladder():
    """Which creature counts as "a floor boss" stays a judgement while this holds.

    The rule takes any creature the game already calls a boss, which is the same
    line The Nihil's Embrace's cleanse draws. IF THE ROW EVER NAMES A RUNG, that
    stops being a judgement and the reading has to be checked against the row.
    """
    words = flat(rows()["Famine_Wasting_Sickness"]["Description"]).lower()

    named = [rung for rung in ("common", "elite", "legendary", "herald",
                               "cataclysm boss")
             if rung in words]
    assert not named, (
        f"The Wasting Sickness row now names {named}. Which creature satisfies "
        "'a floor boss' is no longer a judgement; check it against the row and "
        "update docs/DECISIONS.md.")


def test_wasting_sicknesses_constants_still_describe_a_chance_and_a_survivable_cap():
    """The two relationships the rule's shape depends on, checked where CI looks.

    WHY THIS DUPLICATES TWO `static_assert`s, for the reason the Mortal Decay
    pair above gives: both are compile-time, only a C++ build can fire them, and
    the build job is not a required check on a pull request. The fast suite is.
    """
    chance = constant("WastingSicknessChancePercentPerHit")
    per_stack = constant("WastingSicknessPercentPerStack")

    most = re.search(r"\bWastingSicknessMostStacks\s*=\s*([0-9]+)\s*;",
                     EFFECTS_HEADER.read_text(encoding="utf-8"))
    assert most, "WastingSicknessMostStacks is not declared in the effects header"
    stacks = int(most.group(1))

    assert 0.0 < chance < 100.0, (
        f"Wasting Sickness inflicts its debuff at {chance}%. At 100 every landed "
        "blow inflicts a stack, which the row does not ask for -- it says "
        "'Enemies have a chance to inflict' -- and at 0 the row does nothing.")
    assert per_stack * stacks < 100.0, (
        f"Wasting Sickness at its cap takes {per_stack * stacks}% of both "
        "maximums. The row describes a debuff to fight through and cure at a "
        "boss, not one that removes the character.")


def test_grasping_tentacles_states_no_number_of_its_own():
    """Every figure this rule uses is borrowed or judged: its row states none.

    IF THE ROW EVER STATES ONE, this fails, so the C++ constants are checked
    against it and `docs/DECISIONS.md` stops calling them judgements.
    """
    words = flat(rows()["Void_Grasping_Tentacles"]["Description"])

    assert "%" not in words, words
    assert not [c for c in words if c.isdigit()], (
        "The Grasping Tentacles row now states a number. Check "
        "GraspingTentaclesMostOnAFloor, GraspingTentaclesGrabChancePercentPerBeat, "
        "GraspingTentaclesGrabSeconds, GraspingTentaclesGrabCooldownSeconds and "
        "GraspingTentaclesGrabMovementLessPercent against it and update "
        "docs/DECISIONS.md.")


def test_grasping_tentacles_says_grabbed_and_restricts_movement_and_not_more():
    """The wordings the ruling rests on, and the one that bounds it.

    "GRABBED" IS WHY A GRAB IS AN EVENT WITH AN END rather than a state the
    player stands in. The rule holds for a fixed time and releases; a tentacle
    then waits before it can grab again.

    "RESTRICTING THEIR MOVEMENT" IS WHY IT IS NOT A STUN. The rule takes 99% of
    movement speed through the stat pipeline, which leaves the character able to
    attack. `Debuff_Stun` stops the target acting entirely, and the row does not
    ask for that.

    "GETTING TOO CLOSE" IS WHY THERE IS A REACH AND A CHANCE rather than a toll
    on the whole floor.

    THE ROW NAMES NO DAMAGE, which is why a tentacle deals none. If it ever does,
    the zone is spawned with a damage of zero and that has to be revisited.
    """
    words = flat(rows()["Void_Grasping_Tentacles"]["Description"]).lower()

    assert "grabbed" in words, words
    assert "restricting their movement" in words, words
    assert "too close" in words, words

    harm = [word for word in ("damage", "damaging", "health", "kill", "dealing")
            if word in words]
    assert not harm, (
        f"The Grasping Tentacles row now names {harm}. A tentacle is spawned "
        "with no damage per tick because the row asked for none; check the rule "
        "against the row and update docs/DECISIONS.md.")


def test_grasping_tentacles_is_not_the_singularity_wells_row():
    """The two Void rows must keep describing different things.

    WHY THIS EXISTS. "Grabbed, restricting their movement" was read as an EVENT
    -- a hold that ends and a tentacle that then waits -- partly because reading
    it as a continuous slow while inside a reach would have made this row a
    near-duplicate of `Void_Singularity_Wells`, whose own row says "slowing
    movement by 40%". That reasoning is recorded in `docs/DECISIONS.md`.

    THIS IS A REMINDER AND NOT A GUARD, and says so. It fails if the Singularity
    Wells row stops describing a slow, which is the moment to re-read why this
    row was built as an event instead.
    """
    wells = flat(rows()["Void_Singularity_Wells"]["Description"]).lower()

    assert "slowing movement" in wells, (
        "The Singularity Wells row no longer describes a slow. Grasping "
        "Tentacles was built as a timed grab rather than a continuous slow "
        "partly to avoid duplicating it; re-read that reasoning in "
        "docs/DECISIONS.md.")


def test_grasping_tentacles_constants_keep_the_shape_the_ruling_asked_for():
    """The three relationships the rule depends on, checked where CI looks.

    WHY THIS DUPLICATES `static_assert`s, for the reason the two rules above give:
    all of them are compile-time, only a C++ build can fire them, and the build
    job is not a required check on a pull request. The fast suite is.
    """
    grab = constant("GraspingTentaclesGrabSeconds")
    cooldown = constant("GraspingTentaclesGrabCooldownSeconds")
    less = constant("GraspingTentaclesGrabMovementLessPercent")
    reach = constant("GraspingTentaclesReachCm")
    within = constant("GraspingTentaclesAppearWithinCm")

    assert cooldown > grab, (
        f"A tentacle may grab again after {cooldown}s while its grab lasts "
        f"{grab}s, so a player standing in a reach is held without a break. The "
        "cooldown being longer than the grab is what makes this repeated grabs "
        "rather than a permanent hold.")
    assert 0.0 < less < 100.0, (
        f"A grab takes {less}% of movement speed. At 100 the character cannot "
        "move at all, which is a stun -- and this rule is deliberately not one, "
        "because a grabbed character can still act.")
    assert within > reach, (
        f"A tentacle appears within {within}cm and reaches {reach}cm, so it can "
        "no longer land clear of the player -- it would grab the moment it "
        "appeared, and 'careful of getting too close' would describe nothing the "
        "player chose.")


def test_the_edict_of_silence_still_states_its_own_two_numbers():
    """The only row of the four built in this pass whose figures came with it.

    BOTH ARE THE ROW'S: "Every 90 seconds ... for 15 seconds". So neither is a
    judgement, and this is what fails if the row and the rule drift apart -- the
    case where the game silences on one schedule and the floor panel tells the
    player another.
    """
    words = flat(rows()["Celestial_Edict_of_Silence"]["Description"])
    every = constant("EdictOfSilenceEverySeconds")
    lasts = constant("EdictOfSilenceLastsSeconds")

    assert f"Every {every:g} seconds" in words, words
    assert f"for {lasts:g} seconds" in words, words


def test_the_edict_of_silence_states_no_third_number():
    """Its row gives two figures and the rule uses exactly those two.

    THE LOCK VALUE IS NOT A THIRD FIGURE. Everything that reads the skill-lock
    stat asks only whether it is above zero, so one is how "above zero" is
    written rather than a size anybody chose. IF THE ROW EVER STATES A THIRD
    NUMBER -- a radius, a share, a count -- this fails, and whatever it names has
    to be checked against the rule.
    """
    words = flat(rows()["Celestial_Edict_of_Silence"]["Description"])

    assert "%" not in words, words
    assert [c for c in words if c.isdigit()] == ["9", "0", "1", "5"], (
        "The Edict of Silence row now states a number besides its 90 and 15. "
        "Check it against EdictOfSilenceEverySeconds, EdictOfSilenceLastsSeconds "
        "and EdictOfSilenceLockValue, and update docs/DECISIONS.md.")


def test_the_edict_of_silence_says_all_skills_and_spares_basic_attacks():
    """Two wordings the rule rests on, and both are load-bearing.

    "PREVENTING ALL SKILL USAGE" IS WHY THE LOCK IS UNSCOPED. The stat is read
    through `StatForSkill` with the skill's own tags, so a value carrying
    `RequiredTags` reaches only skills that match -- which is what the two
    enchantment rows in `game/Data/EnchantmentEffects.csv` do, each to one slot.
    This row's modifier carries no tags at all.

    "ONLY BASIC ATTACKS FUNCTION" IS ALREADY BUILT AND NEEDED NO CODE HERE.
    `UCataclysmSkillTemplate::CanActivateAbility` skips the lock check for that
    slot unconditionally, and `Cataclysm.Skills.ABasicAttackSurvivesALockOnEverySkill`
    is the test that holds it. If the row stops saying this, that exemption
    becomes a thing this rule relies on for no stated reason.
    """
    words = flat(rows()["Celestial_Edict_of_Silence"]["Description"]).lower()

    assert "preventing all skill usage" in words, words
    assert "only basic attacks function" in words, words


def test_the_edict_of_silence_says_it_sweeps_the_dungeon_not_the_floor():
    """The word the clock-across-the-stairs ruling rests on.

    THE ROW SAYS "SWEEPS THE DUNGEON". Every other rule in that file describes
    something happening on a floor, and every other rule forgets its clock when
    the floor changes. This one keeps its clock across the stairs because the
    sentence names the dungeon, which is the one place the two readings of it
    differ in play: a player descending faster than the cadence would otherwise
    never be silenced.

    THIS IS A REMINDER AND NOT A GUARD, and says so. It fails if the row stops
    saying "dungeon", which is the moment to re-read that ruling in
    `docs/DECISIONS.md`. It cannot notice the game gaining floors that stay
    alive, which is the other thing that would retire the reading.
    """
    words = flat(rows()["Celestial_Edict_of_Silence"]["Description"]).lower()

    assert "sweeps the dungeon" in words, (
        "The Edict of Silence row no longer says its silence sweeps the dungeon. "
        "Its clock is kept across the stairs on the strength of that word; "
        "re-read the ruling in docs/DECISIONS.md.")


def test_the_artillery_strike_still_states_its_own_cadence():
    """The one number this row gives, held against the constant that uses it.

    EVERYTHING ELSE ABOUT THIS RULE IS A JUDGEMENT -- the warning, the radius and
    the damage are all recorded as such in docs/DECISIONS.md, because the row
    gives none of them. Thirty seconds is the exception, so it is the one figure
    a reader can check the code against without reading a design argument.
    """
    words = flat(rows()["War_Artillery_Strike"]["Description"])
    every = constant("ArtilleryStrikeSecondsBetween")

    assert f"Every {every:g} seconds" in words, words


def test_the_artillery_strike_row_still_says_it_hits_both_sides():
    """The sentence the everyone-search rests on.

    "ENEMIES AND PLAYERS CAN BE HIT" IS WHY THIS RULE ASKS
    `UCataclysmTargeting::FindEveryoneInLine` RATHER THAN `FindEnemiesInLine`.
    Almost every hazard in the game belongs to whoever made it and spares their
    own side; this one belongs to the floor and spares nobody, and the row is the
    whole authority for that.

    IT IS ALSO WHY A STANDING RULE WAS RULED NOT TO APPLY.
    `tools/tests/test_hellhound_matches_the_model.py::test_nothing_burns_its_own_side`
    records "A creature does not burn itself or its own side"; the coordinating
    session ruled on 2026-09-14 that a dungeon hazard belongs to no side, so that
    rule is not engaged. If this sentence ever leaves the row, that ruling has
    nothing left to stand on.
    """
    words = flat(rows()["War_Artillery_Strike"]["Description"]).lower()

    assert "enemies and players can be hit" in words, (
        "The Artillery Strike row no longer says enemies and players can both be "
        "hit. The rule asks for everyone standing in the circle on the strength "
        "of that sentence; re-read the ruling in docs/DECISIONS.md.")


def test_the_artillery_strike_warning_is_longer_than_the_walk_out():
    """The derivation behind the one number that was not simply chosen.

    THE WARNING IS NOT A TASTE. It is twice the time it takes to walk out of the
    circle from its centre at the player's base speed, which is what makes it
    survivable when `Void_Singularity_Wells` is applying its 40% slow on the same
    floor. This holds the three constants in that relationship, so a later change
    to the radius or the warning fails here rather than quietly making the
    warning too short to use.

    THE WALK SPEED IS READ FROM THE PLAYER RATHER THAN WRITTEN DOWN HERE, so this
    cannot drift from the figure the game actually moves at.
    """
    warning = constant("ArtilleryStrikeWarningSeconds")
    radius = constant("ArtilleryStrikeRadiusCm")
    speed = constant("DefaultWalkSpeedCmPerSecond",
                     "game/Source/Cataclysm/Character/CataclysmPlayerCharacter.h")

    walk_out = radius / speed
    assert warning >= walk_out * 2.0, (
        f"The warning is {warning}s and walking out of a {radius}cm circle at "
        f"{speed}cm/s takes {walk_out}s, so the warning is no longer twice the "
        "walk-out. Re-derive it; docs/DECISIONS.md carries the derivation.")


def test_hallowed_groundfall_still_states_its_own_cadence():
    """The one number this row gives.

    THE CRATERS' SIZE, LIFE AND NUMBER ARE ALL JUDGEMENTS, derived from this
    cadence or from an existing bound and recorded in docs/DECISIONS.md. Thirty
    seconds is not: the row says it, so it is the figure a reader can check the
    code against without reading a design argument.
    """
    words = flat(rows()["Celestial_Hallowed_Groundfall"]["Description"])
    every = constant("HallowedGroundfallSecondsBetween")

    assert f"every {every:g} seconds" in words.lower(), words


def test_hallowed_groundfall_row_still_splits_its_two_halves_by_side():
    """The sentence the whole design of this rule rests on.

    "BURN PLAYERS AND EMPOWER ENEMIES" IS WHY THE RULE ASKS TWO DIFFERENT
    QUESTIONS. The crater's own sweep finds the floor hazard's enemies, which is
    the player; the beat asks for the PLAYER's enemies, which is every creature.
    Nothing in the rule excludes anybody by name, because those two sets cannot
    overlap -- and that is only correct while the row keeps saying the burning
    and the empowering fall on opposite sides.

    IF THIS SENTENCE EVER CHANGES, `ACraterDoesNotEmpowerThePlayer` is asserting
    something the data no longer asks for.
    """
    words = flat(rows()["Celestial_Hallowed_Groundfall"]["Description"]).lower()

    assert "burn players" in words, words
    assert "empower enemies" in words, words


def test_hallowed_groundfall_craters_do_not_outlast_the_gap_between_them():
    """The relationship that makes a cap on craters unnecessary.

    THE RULE ASKS FOR NO ALIVE COUNT, unlike Infernal Rain and Grasping
    Tentacles, which both place ONE thing on a short clock and need a cap to stop
    a floor filling up. This one places a fixed number on a long clock: the
    craters are gone before the next bombardment arrives, so the floor can never
    carry more than one bombardment's worth.

    THAT IS ARITHMETIC AND NOT A PROMISE, so it is held here as well as by a
    static assertion in the header. If a crater ever outlasts the gap, the rule
    needs a cap and this fails first.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    # THE DERIVATION IS WHAT IS HELD, NOT A NUMBER. The crater's life is declared
    # as half the cadence rather than as a figure, so that a later change to the
    # cadence carries the crater with it. Replacing it with a literal would break
    # that link silently, which is what this notices.
    declared = re.search(
        r"HallowedGroundfallCraterSeconds\s*=\s*"
        r"HallowedGroundfallSecondsBetween\s*/\s*2\.0f\s*;",
        text)

    assert declared, (
        "The crater's life is no longer declared as half the cadence. It was "
        "derived from the row's own 'every 30 seconds' so the floor alternates "
        "dangerous and clear; if it is now a figure of its own, re-derive it and "
        "check it still ends before the next bombardment. The rule carries no cap "
        "on craters because it never needed one. docs/DECISIONS.md has the "
        "derivation.")
def test_spore_clouds_row_still_states_a_chance_and_no_figure_for_it():
    """The gap the rule's chance fills, held open.

    THE ROW SAYS "a chance" AND GIVES NO PERCENTAGE, which is why
    `SporeCloudsChancePercentOnDeath` is a judgement rather than a reading. Eight
    of the 117 rows are written that way and this is one of them.

    IF THE ROW EVER STATES A FIGURE, THE RULE MUST USE THAT ONE. A row saying
    "a 25% chance" beside a rule using ten is the exact drift this whole file
    exists to catch, and nothing else would notice it: the C++ tests build the
    chance from the constant, so they would agree with themselves either way.
    """
    words = flat(rows()["Pestilence_Spore_Clouds"]["Description"])

    assert "a chance" in words.lower(), words
    assert "%" not in words, (
        "Pestilence_Spore_Clouds now states a percentage. The rule's chance was "
        "a judgement made because the row gave none -- see "
        "SporeCloudsChancePercentOnDeath in CataclysmDungeonModifierEffects.h. "
        "Use the row's figure instead. " + words)


def test_spore_clouds_row_still_names_the_trigger_the_rule_listens_for():
    """A death, and an enemy's death.

    THE RULE IS BOUND TO `OnSomethingDied` AND REFUSES EVERY VICTIM THAT IS NOT A
    CREATURE. Both of those read this sentence. A row that moved its trigger to a
    hit, or to the player's death, would leave the rule listening to the wrong
    announcement and nothing in C++ would say so.
    """
    words = flat(rows()["Pestilence_Spore_Clouds"]["Description"]).lower()

    assert "enemies" in words, words
    assert "on death" in words, words


def test_spore_clouds_row_still_names_poison_and_the_player():
    """Which ailment, and on whom.

    THE RULE APPLIES `DoT_Poison` AND NOTHING ELSE, and it applies it to the
    player rather than to whatever is nearby. Both come from this sentence. The
    C++ test `SporesFromADeathNearThePlayerPoisonThem` asserts the player carries
    Poison and not Burn; if the row stopped naming poison, that assertion would be
    holding the rule to a word the design no longer uses.
    """
    words = flat(rows()["Pestilence_Spore_Clouds"]["Description"]).lower()

    assert "poison" in words, words
    assert "the player" in words, words


def test_spore_clouds_chance_is_still_the_figure_the_table_uses_for_a_death():
    """The row the rule's ten was derived from.

    `Death_Vengful_Wraiths` IS THE ONLY ROW STATING A FIGURE FOR A CHANCE FIRED BY
    ANY ENEMY'S DEATH, measured across all 117 on 2026-09-14, and it says ten. That
    is the whole derivation for `SporeCloudsChancePercentOnDeath`, which its own
    comment records.

    IF THAT ROW'S FIGURE MOVES, THE DERIVATION IS GONE and the rule's ten is a
    number somebody once chose. This fails then, which is the point: it holds an
    argument rather than a value.
    """
    anchor = flat(rows()["Death_Vengful_Wraiths"]["Description"])
    chance = constant("SporeCloudsChancePercentOnDeath")

    assert f"{chance:g}% chance" in anchor.lower(), (
        "Death_Vengful_Wraiths no longer states a "
        f"{chance:g}% chance. Spore Clouds' chance was derived from it as the "
        "figure this table already uses for a chance on a death -- see "
        "SporeCloudsChancePercentOnDeath in CataclysmDungeonModifierEffects.h and "
        "docs/DECISIONS.md. Re-derive it. " + anchor)


def test_spore_clouds_reach_is_still_declared_as_the_other_deaths_radius():
    """The derivation, not the number.

    SPORES RELEASED AT A CORPSE AND A PATCH LEFT AT A CORPSE ASK ONE QUESTION --
    how far does a thing left by a death reach -- and this project answered it
    once, for `Famine_Withered_Ground`. The reach is declared as that constant
    rather than as 300, so a later change to one carries the other.

    REPLACING IT WITH A LITERAL WOULD BREAK THAT LINK SILENTLY, which is what this
    notices. It is the same shape as
    `test_hallowed_groundfall_craters_do_not_outlast_the_gap_between_them` above.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    declared = re.search(
        r"SporeCloudsReachCm\s*=\s*WitheredGroundPatchRadiusCm\s*;", text)

    assert declared, (
        "Spore Clouds' reach is no longer declared as WitheredGroundPatchRadiusCm. "
        "It was copied from that rule as a conclusion, because a patch left by a "
        "death and spores released by one are the same question and the project "
        "has answered it. If the reach is now a figure of its own, say why in "
        "docs/DECISIONS.md and give it its own derivation.")
def test_hellfire_row_still_states_a_chance_and_no_figure_for_it():
    """The gap the rule's chance fills, held open.

    THE SAME SHAPE AS `test_spore_clouds_row_still_states_a_chance_and_no_figure_
    for_it` ABOVE, and both are needed: each row can gain a percentage without the
    other doing so, and a rule using ten beside a row saying twenty-five is the
    drift this whole file exists to catch.
    """
    words = flat(rows()["Demonic_Hellfire"]["Description"])

    assert "a chance" in words.lower(), words
    assert "%" not in words, (
        "Demonic_Hellfire now states a percentage. The rule's chance was a "
        "judgement made because the row gave none -- see "
        "HellfireChancePercentOnDeath in CataclysmDungeonModifierEffects.h. Use "
        "the row's figure instead. " + words)


def test_hellfire_row_still_names_the_trigger_the_rule_listens_for():
    """An enemy, and its death.

    THE RULE IS BOUND TO `OnSomethingDied` AND REFUSES EVERY VICTIM THAT IS NOT A
    CREATURE. A row that moved its trigger to a hit, or to the player's death,
    would leave the rule listening to the wrong announcement and nothing in C++
    would say so.
    """
    words = flat(rows()["Demonic_Hellfire"]["Description"]).lower()

    assert "enemies" in words, words
    assert "killed" in words, words


def test_hellfire_row_names_an_explosion_and_not_an_ailment():
    """What separates this row from the other chance-on-death row beside it.

    `Pestilence_Spore_Clouds` NAMES AN AILMENT -- "spores ... that poison the
    player" -- AND ITS RULE APPLIES `DoT_Poison`. This row names an explosion and
    no lasting effect, so its rule deals one blow. Reading this one as an ailment
    too would make the two rows the same rule under two names, which is the
    argument `UCataclysmEnemyModifiers` already makes about the Commander buff.

    IF THIS ROW EVER NAMES AN AILMENT, the rule needs rewriting rather than
    extending, and this fails first.
    """
    words = flat(rows()["Demonic_Hellfire"]["Description"]).lower()

    assert "explode" in words, words
    for ailment in ("poison", "burn", "bleed", "disease", "necrosis"):
        assert ailment not in words, (
            f"Demonic_Hellfire now names the ailment {ailment!r}. Its rule deals "
            "a one-off blow because the row named an explosion and nothing "
            "lasting -- see HellfireKey in CataclysmDungeonModifierEffects.h. " +
            words)


def test_hellfire_reach_is_still_declared_as_the_floors_own_radius():
    """The derivation, not the number.

    300 IS THIS PROJECT'S SETTLED ANSWER FOR A THING AT A POINT ON THE FLOOR --
    Infernal Rain's patch, Singularity Wells, Withered Ground's patch, Grasping
    Tentacles' reach and Spore Clouds' reach are all that figure. The explosion is
    declared as `InfernalRainRadiusCm` rather than as 300 so that a later change
    to one carries the other.

    NOT `ArtilleryStrikeRadiusCm`, WHICH IS 600, because that figure belongs to a
    circle its own row calls massive and this row says nothing about size.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    declared = re.search(r"HellfireRadiusCm\s*=\s*InfernalRainRadiusCm\s*;", text)

    assert declared, (
        "Hellfire's radius is no longer declared as InfernalRainRadiusCm. It was "
        "copied from that rule as a conclusion because 300 is what this project "
        "uses for a thing at a point on the floor. If it is now a figure of its "
        "own, say why in docs/DECISIONS.md and give it its own derivation.")


def test_hellfire_damage_is_read_off_the_creature_and_not_written_down():
    """The best property of this rule, held in the source.

    THE EXPLOSION IS WORTH THE DYING CREATURE'S OWN ATTACK DAMAGE, which is what
    `UCataclysmEnemyModifiers::InfernalBrand` already does and says why: "Read off
    the creature rather than written here, so a Herald's brand is a Herald's
    brand." A deeper floor's creatures then explode harder with no scaling code.

    A CONSTANT WOULD PASS EVERY OTHER CHECK IN THIS FILE. The chance, the reach
    and the trigger would all still hold while every creature exploded alike, so
    this is the one that notices. The automation test
    `AKilledEnemyExplodesOnWhoeverIsStandingNearIt` kills two creatures of
    different attack damage for the same reason.
    """
    source = EFFECTS_SOURCE.read_text(encoding="utf-8")

    multiplies = re.search(
        r"HellfireDamage\s*\(\s*float\s+CreatureAttackDamage\s*\)"
        r"[\s\S]{0,400}?return\s+CreatureAttackDamage\s*\*\s*"
        r"HellfireExplosionHits\s*;",
        source)

    assert multiplies, (
        "HellfireDamage no longer multiplies the creature's own attack damage by "
        "HellfireExplosionHits. The whole point of the rule's magnitude is that it "
        "is read off the creature rather than written down -- see HellfireKey in "
        "CataclysmDungeonModifierEffects.h and InfernalBrandExplosionHits in "
        "CataclysmEnemyModifiers.h. A constant makes every creature explode alike.")
