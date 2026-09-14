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
stacks a reduction to both of the player's maximums when an enemy's blow lands.

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


def constant(name: str) -> float:
    """A `static constexpr float` from the effects header, by name."""
    text = EFFECTS_HEADER.read_text(encoding="utf-8")
    found = re.search(rf"\b{re.escape(name)}\s*=\s*([0-9]+(?:\.[0-9]+)?)f\s*;", text)
    assert found, f"{name} is not declared in {EFFECTS_HEADER.name}"
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
