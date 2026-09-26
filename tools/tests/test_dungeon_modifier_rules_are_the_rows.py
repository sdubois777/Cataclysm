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


def body_of(text: str, opening: str) -> str:
    """The braced body that follows `opening`, by counting braces.

    A FIFTH COPY OF THIS FUNCTION, AND THAT IS SEEN RATHER THAN CARELESS.
    `tools/tests/` already holds four -- in the floor-effects reader test, the
    condition-list test, the screen-press test and the hooks test -- because
    there is no shared helper module for this suite, and inventing one for a
    fifth caller is a larger change than the one it would serve. Said here so
    the next person counting copies knows it was counted.

    A REGEX CANNOT DO THIS. Every function it is pointed at contains nested
    braces, and a lazy match to the first closing brace silently returns a
    fragment. A fragment is the dangerous answer: anything after the cut reads
    as absent.
    """
    start = text.index(opening)
    depth = 0
    for index in range(text.index("{", start), len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"braces never balanced after {opening!r}")


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
def whole_number(name: str, header: str = "") -> int:
    """A `static constexpr int32` by name, from the effects header by default.

    SEPARATE FROM `constant` ABOVE BECAUSE THE SUFFIX DIFFERS. That one requires
    the `f` a float literal carries, which is what stops it matching a count by
    accident; a whole number has none, so it needs its own reader rather than a
    loosened one.
    """
    where = REPO_ROOT / header if header else EFFECTS_HEADER
    text = where.read_text(encoding="utf-8")
    found = re.search(rf"\b{re.escape(name)}\s*=\s*(-?[0-9]+)\s*;", text)
    assert found, f"{name} is not declared as a whole number in {where.name}"
    return int(found.group(1))


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
def test_fungal_overgrowth_row_still_states_both_of_its_shares():
    """Both of this rule's figures are READ OFF THE ROW rather than judged.

    "grants either a 50% speed boost or a 50% slow". Two shares, both stated, so
    the rule decides neither. This is what holds each reading to the thing read.

    THE TWO ARE CHECKED SEPARATELY THOUGH THEY ARE THE SAME NUMBER TODAY. A row
    that moved one of them would leave the other correct, and a single check
    written against one constant would pass while the rule under-read the row by
    half.

    NOTHING IN C++ WOULD NOTICE EITHER MOVING. The automation tests build every
    expectation from these constants, so they agree with themselves at any value.
    """
    words = flat(rows()["Pestilence_Fungal_Overgrowth"]["Description"])
    boost = constant("FungalOvergrowthSpeedMorePercent")
    slow = constant("FungalOvergrowthSpeedLessPercent")

    assert f"{boost:g}% speed boost" in words, (
        f"The row no longer says '{boost:g}% speed boost'. "
        "FungalOvergrowthSpeedMorePercent in CataclysmDungeonModifierEffects.h "
        "is read off this sentence, so move it to whatever the row now says. "
        + words)
    assert f"{slow:g}% slow" in words, (
        f"The row no longer says '{slow:g}% slow'. "
        "FungalOvergrowthSpeedLessPercent in CataclysmDungeonModifierEffects.h "
        "is read off this sentence. " + words)


def test_fungal_overgrowth_row_still_names_its_trigger_and_what_the_player_does():
    """The two events the rule is wired to, held as the row's own words.

    "Killing enemies creates mushrooms" is why a listener sits on the death
    announcement rather than on a clock -- the shape Withered Ground has.
    "Stepping on them" is why the quarter-second beat asks `Covers` rather than
    the rule applying anything when a mushroom is placed.

    A ROW THAT MOVED EITHER WOULD LEAVE THE RULE WIRED TO THE WRONG THING while
    every figure above still matched.
    """
    words = flat(rows()["Pestilence_Fungal_Overgrowth"]["Description"]).lower()

    assert "killing enemies" in words, (
        "Pestilence_Fungal_Overgrowth no longer says its mushrooms come from "
        "killing enemies. NoteDeathForFungalOvergrowth in "
        "CataclysmDungeonGameMode.h listens to the death announcement because of "
        "this sentence. " + words)
    assert "stepping on them" in words, (
        "Pestilence_Fungal_Overgrowth no longer says the player steps on them. "
        "StepFungalOvergrowth asks each mushroom whether it covers the player, "
        "on the beat, because of this sentence. " + words)


def test_fungal_overgrowth_row_states_no_chance_that_a_mushroom_appears():
    """Every kill leaves one, and this is what says the row asks for that.

    `Pestilence_Spore_Clouds` and `Demonic_Hellfire` both say "chance" and both
    therefore roll for whether anything happens at all.
    `FungalOvergrowthBoostChancePercent` is NOT that kind of figure: it decides
    which of two kinds a mushroom is, and never whether there is one.

    IF THIS ROW EVER SAYS "CHANCE", the rule is under-building it -- there would
    be a second roll to make -- and this fails before anybody reads the code.
    """
    words = flat(rows()["Pestilence_Fungal_Overgrowth"]["Description"]).lower()

    assert "chance" not in words, (
        "Pestilence_Fungal_Overgrowth now states a chance. "
        "NoteDeathForFungalOvergrowth leaves a mushroom on EVERY creature death "
        "and rolls only for which kind, because this row stated no chance -- the "
        "way Famine_Withered_Ground states none. A chance here needs a second "
        "roll and a constant of its own. " + words)


def test_fungal_overgrowths_split_radius_and_colours_are_judgements_not_the_rows():
    """The four things this rule decides, held as things the row does NOT say.

    The row states two shares and nothing else: no odds between the two kinds, no
    size, no lifetime, no appearance. Each of those is answered beside its
    constant in `CataclysmDungeonModifierEffects.h` and recorded in
    `docs/DECISIONS.md`.

    THE DAY THE ROW STATES ONE, THE JUDGEMENT STOPS BEING A JUDGEMENT and the
    constant has to be read off the row instead. That is what this notices.
    `test_dehydrations_cap_is_still_a_judgement_and_not_the_rows` above is the
    same shape.
    """
    words = flat(rows()["Pestilence_Fungal_Overgrowth"]["Description"]).lower()

    for unsaid in ("cm", "metre", "meter", "radius", "second", "colour", "color"):
        assert unsaid not in words, (
            f"Pestilence_Fungal_Overgrowth now says '{unsaid}'. Its mushroom's "
            "size, lifetime and colours are judgements recorded beside the "
            "constants in CataclysmDungeonModifierEffects.h. If the row now "
            "states one, read it off the row and say so there. " + words)

    # THE SPLIT, SEPARATELY, because "50" appears twice in this row already and a
    # search for the bare number would match the shares the rule DOES read off it.
    assert "half" not in words and "even" not in words, (
        "Pestilence_Fungal_Overgrowth now describes the odds between its two "
        "kinds. FungalOvergrowthBoostChancePercent is an even split because the "
        "row named two outcomes and no odds. " + words)


def test_fungal_overgrowth_mushroom_radius_is_still_a_derivation():
    """The size, held as a derivation rather than as a number.

    300 IS THIS PROJECT'S SETTLED ANSWER for a piece of ground a player stands
    on -- Withered Ground's patch, a Singularity Well, Infernal Rain's patch and
    the Gatekeeper's Soulfall all use it -- so a mushroom is declared as
    `WitheredGroundPatchRadiusCm` and a later change to one carries the other.
    Same shape as `test_hellfire_reach_is_still_declared_as_the_other_deaths_
    radius` above.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    declared = re.search(
        r"FungalOvergrowthMushroomRadiusCm\s*=\s*WitheredGroundPatchRadiusCm\s*;",
        text)

    assert declared, (
        "A mushroom's radius is no longer declared as WitheredGroundPatchRadiusCm. "
        "It was copied as a conclusion because 300 is what this project uses for "
        "a piece of ground a player stands on. If it is now a figure of its own, "
        "say why in docs/DECISIONS.md and give it its own derivation.")


def test_the_two_kinds_of_mushroom_are_drawn_in_two_real_and_different_colours():
    """The colours name rows that exist, and they are not the same row.

    WHY BOTH HALVES MATTER. `UCataclysmElementVisuals::ColoursFor` answers false
    for a name it cannot find and leaves the drawing system's authored white, so
    a colour naming no row of `game/Data/ElementVisuals.csv` makes that kind of
    mushroom look like every untyped zone in the game -- and two colours that are
    the same name make the two kinds indistinguishable, which is the whole reason
    a zone may carry a colour at all.

    NEITHER FAILURE IS AN ERROR ANYWHERE. Nothing logs a missing row and nothing
    compares the two constants, so both faults ship looking like working code.
    """
    source = EFFECTS_SOURCE.read_text(encoding="utf-8")
    visuals = REPO_ROOT / "game" / "Data" / "ElementVisuals.csv"

    names = {}
    for which in ("FungalOvergrowthBoostDrawnAs", "FungalOvergrowthSlowDrawnAs"):
        found = re.search(rf'{which}\s*=\s*TEXT\("([^"]+)"\)\s*;', source)
        assert found, (
            f"{which} is no longer defined as a TEXT literal in "
            f"{EFFECTS_SOURCE.name}. It names a row of {visuals.name} and this "
            "check reads it from there.")
        names[which] = found.group(1)

    with visuals.open(newline="", encoding="utf-8") as handle:
        rows_available = {row["Name"] for row in csv.DictReader(handle)}

    assert len(rows_available) > 1, (
        f"{visuals.name} parsed to {len(rows_available)} row(s), which is too few "
        "to be the real file, so the assertions below would pass having compared "
        "nothing.")

    for which, name in names.items():
        assert name in rows_available, (
            f"{which} is {name!r}, which is not a row of {visuals.name}. "
            "UCataclysmElementVisuals::ColoursFor answers false for a name it "
            "cannot find and leaves the authored white, so that kind of mushroom "
            f"would look like every untyped zone. Rows: {sorted(rows_available)}")

    boost = names["FungalOvergrowthBoostDrawnAs"]
    slow = names["FungalOvergrowthSlowDrawnAs"]
    assert boost != slow, (
        f"Both kinds of mushroom are drawn as {boost!r}. The two colours exist so "
        "a player can tell a mushroom that helps from one that hurts; the same "
        "name for both makes ACataclysmGroundZone::DrawnAsType pointless for this "
        "rule.")


def test_illusory_enemies_row_still_states_no_figure_of_its_own():
    """The share of a floor that is illusions is a JUDGEMENT, and this says so.

    "Some enemies are illusions" is the only quantity the row gives, and "some"
    is not a number. `IllusoryEnemiesSharePercent` is 25 because this rule works
    by uncertainty and fails in both directions: too many and the floor stops
    being dangerous, too few and a player finishes a dungeon without meeting one.

    THE DAY THE ROW STATES A FIGURE, THE JUDGEMENT STOPS BEING ONE and the
    constant has to be read off the row instead. That is what this notices.
    `test_dehydrations_cap_is_still_a_judgement_and_not_the_rows` above is the
    same shape.
    """
    words = flat(rows()["Chaos_Illusory_Enemies"]["Description"])
    share = constant("IllusoryEnemiesSharePercent")

    assert not re.search(r"\d", words), (
        f"Chaos_Illusory_Enemies now states a number: {words!r}. "
        f"IllusoryEnemiesSharePercent is {share:g} as a judgement recorded "
        "beside the constant in CataclysmDungeonModifierEffects.h, because the "
        "row gave none. If the row now gives one, read it off the row and say "
        "so there.")
    assert "some" in words.lower(), (
        "Chaos_Illusory_Enemies no longer says 'some'. The rule makes a SHARE "
        "of each floor's creatures illusions because of that word; a row saying "
        "'all', or naming particular enemies, wants something else entirely. "
        + words)


def test_illusory_enemies_row_still_says_the_illusions_do_no_damage():
    """The whole mechanism, held to the words it came from.

    "do no damage" is why the rule zeroes one attribute and changes nothing
    else. A row that instead said an illusion vanishes when hit, or has no
    health, or grants nothing, would need a different rule entirely.
    """
    words = flat(rows()["Chaos_Illusory_Enemies"]["Description"]).lower()

    assert "do no damage" in words, (
        "Chaos_Illusory_Enemies no longer says its illusions do no damage. That "
        "sentence is the whole rule: ACataclysmEnemyCharacter::bIsAnIllusion "
        "zeroes the one attribute every creature damage route reads, and "
        "nothing else about the creature changes. " + words)


def test_illusory_enemies_row_still_says_they_are_otherwise_ordinary():
    """Why the rule changes NOTHING but the damage, held to the row.

    "They look and act like real enemies" is the reason an illusion keeps its
    kind's health, its rarity, its modifiers, its death announcement and its
    rewards. A reader who assumed "illusion" meant "not really there" would
    change several of those; the row says the opposite, and this is what says so.
    """
    words = flat(rows()["Chaos_Illusory_Enemies"]["Description"]).lower()

    assert "look and act like real enemies" in words, (
        "Chaos_Illusory_Enemies no longer says its illusions look and act like "
        "real enemies. That sentence is why the rule changes one attribute and "
        "leaves health, rarity, modifiers, death and rewards alone. " + words)


def test_the_illusion_is_honoured_where_a_creatures_damage_is_recomputed():
    """The design decision, held in the source because nothing else holds it.

    WHY IT MATTERS. Six public setters on `ACataclysmEnemyCharacter` re-run
    `ApplyStartingAttributes`, and each recomputes attack damage from the
    creature's designed figure. If the illusion were a zero written once at floor
    population rather than a flag that recompute honours, any of the six would put
    the damage back.

    **A C++ TEST DOES COVER THAT** -- it calls all six and asserts the creature
    stays harmless. This checks something narrower: that the decision still LIVES
    where every recompute writes the damage. A later change could satisfy the C++
    test by writing the zero somewhere that happens to run after every setter today,
    and would then break silently the first time a seventh caller arrived.

    THE DECISION MOVED ONE CALL DOWN IN ISSUES #1820 AND #41, AND THIS CHECK MOVED
    WITH IT. `WriteAttackDamage` writes the attack damage for
    `ApplyStartingAttributes` and for the two floor-rule setters,
    `SetPlacedDamageMultiplier` and `SetTimeAliveDamageMultiplier`, which must not
    re-run the whole recompute because that refills health. So the flag has to be
    read in the helper, and the recompute the six setters re-run has to call the
    helper -- in its code, not in a comment.
    """
    source = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
              / "CataclysmEnemyCharacter.cpp").read_text(encoding="utf-8")

    helper = body_of(source, "void ACataclysmEnemyCharacter::WriteAttackDamage")
    recompute = "\n".join(
        line for line in body_of(
            source, "void ACataclysmEnemyCharacter::ApplyStartingAttributes").splitlines()
        if not line.lstrip().startswith("//"))

    # THE FLAG BEING *USED*, NOT MERELY NAMED. A comment inside this same
    # function points a reader at `bIsAnIllusion` in the header, so a check for
    # the bare name passes even with the line that reads it deleted -- a check
    # that cannot fail for the thing it was written for. Measured before this
    # was corrected: 4 mentions in the file, 2 of them code.
    assert re.search(r"bIsAnIllusion\s*\?", helper), (
        "WriteAttackDamage in CataclysmEnemyCharacter.cpp no longer READS "
        "bIsAnIllusion when it writes the attack damage. Every write of a creature's "
        "attack damage goes through that helper, and six public setters re-run the "
        "recompute that calls it, so an illusion decided anywhere else is undone by "
        "whichever of them runs next. The field's own declaration carries the "
        "argument.")
    assert "WriteAttackDamage(" in recompute, (
        "ApplyStartingAttributes in CataclysmEnemyCharacter.cpp no longer calls "
        "WriteAttackDamage in its code. The six setters that re-run it would then "
        "write the attack damage some other way, and the illusion's zero and a floor "
        "rule's multiplier, both decided in the helper, would not reach it.")


def test_asking_a_creature_for_zero_attack_damage_is_not_guarded_away():
    """The defect fix, held in the source.

    UNTIL ISSUES #1820 AND #41 the write was guarded with
    `StartingAttackDamage > 0.0f`, so `SetAttackDamage(0)` recorded the zero and
    SKIPPED THE WRITE. The attribute kept whatever it held, so a creature already
    given its designed damage went on dealing it in full with nothing reporting
    anything -- while the declaration of `StartingAttackDamage` claimed the
    opposite in its own second sentence.

    THIS IS NOT WHAT MAKES AN ILLUSION HARMLESS, and the source says so beside
    the guard. It is a separate defect that building the illusion uncovered.

    READ IN `WriteAttackDamage` SINCE RAVENOUS HOARD, which moved the one write of a
    creature's attack damage into that helper. The check above holds that
    `ApplyStartingAttributes` still calls it.
    """
    source = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
              / "CataclysmEnemyCharacter.cpp").read_text(encoding="utf-8")

    body = body_of(source, "void ACataclysmEnemyCharacter::WriteAttackDamage")

    assert re.search(r"StartingAttackDamage\s*>=\s*0\.0f", body), (
        "WriteAttackDamage no longer guards its attack-damage write with "
        "`StartingAttackDamage >= 0.0f`. At `> 0.0f` a zero asked for through "
        "SetAttackDamage is recorded and never written, so a creature keeps the "
        "damage it already had and the declaration of StartingAttackDamage -- "
        "'Zero means it deals nothing' -- becomes false again.")
    assert not re.search(r"StartingAttackDamage\s*>\s*0\.0f", body), (
        "WriteAttackDamage contains a `StartingAttackDamage > 0.0f` "
        "comparison. If the old guard is back, a zero asked for is silently "
        "dropped. If it is a second, deliberate comparison, narrow this check "
        "rather than deleting it.")


def test_holy_repercussions_row_states_no_figure_of_its_own():
    """All five of this rule's figures are JUDGEMENTS, and this is what says so.

    The row gives a chance, an area, a stacking debuff and a direction, and not
    one number: not the chance, the damage, the reach, the cap, or what a stack
    is worth. Each constant in `CataclysmDungeonModifierEffects.h` carries the
    precedent it follows, or says plainly that it has none.

    THE DAY THE ROW STATES ONE, THAT JUDGEMENT STOPS BEING ONE and the constant
    has to be read off the row instead.
    """
    words = flat(rows()["Celestial_Holy_Repercussions"]["Description"])

    assert not re.search(r"\d", words), (
        f"Celestial_Holy_Repercussions now states a number: {words!r}. Its "
        "chance, burst damage, reach, stack cap and per-stack worth are all "
        "judgements recorded beside the constants in "
        "CataclysmDungeonModifierEffects.h, because the row gave none. Read the "
        "new figure off the row and say so there.")


def test_holy_repercussions_row_still_says_a_chance_upon_being_hit():
    """The trigger and its direction, held to the row's own words.

    "a chance" is why the rule rolls. "upon being hit" is why the listener tests
    that the PLAYER is the attacker and a CREATURE the target -- the creature is
    the thing being hit. A row that moved to the player being hit would leave the
    listener reading the blow announcement backwards, and nothing in C++ would
    notice.
    """
    words = flat(rows()["Celestial_Holy_Repercussions"]["Description"]).lower()

    assert "chance" in words, (
        "Celestial_Holy_Repercussions no longer says 'chance'. "
        "HolyRepercussionsRetaliates rolls because of that word; without it every "
        "blow would be answered. " + words)
    assert "upon being hit" in words, (
        "Celestial_Holy_Repercussions no longer says 'upon being hit'. "
        "NoteHitForHolyRepercussions tests that the player is the attacker and a "
        "creature the target because the creature is what is being hit. " + words)


def test_holy_repercussions_row_still_names_holy_damage_and_a_stacking_debuff():
    """Which resistance, and that it stacks.

    "increases holy damage taken" is why Judgment lowers the Celestial
    resistance and no other -- holy is Celestial in this game, which is the
    Cataclysm type the row itself belongs to. "a stacking debuff" is why there is
    a count and a cap rather than a single on-or-off reduction.
    """
    row = rows()["Celestial_Holy_Repercussions"]
    words = flat(row["Description"]).lower()

    assert "holy damage taken" in words, (
        "Celestial_Holy_Repercussions no longer says 'holy damage taken'. "
        "HolyRepercussionsResistance is Celestial because of those words. "
        + words)
    assert row["CataclysmType"] == "Celestial", (
        f"Celestial_Holy_Repercussions is now typed {row['CataclysmType']!r}. "
        "Its Judgment lowers the Celestial resistance on the reading that 'holy' "
        "is this row's own Cataclysm; if the type changed, that reading has to be "
        "made again.")
    assert "stacking" in words, (
        "Celestial_Holy_Repercussions no longer says its debuff stacks. "
        "HolyRepercussionsJudgmentMostStacks and the live count on the floor "
        "panel exist because of that word. " + words)

    declared = re.search(
        r'HolyRepercussionsResistance\s*=\s*TEXT\("([^"]+)"\)\s*;',
        EFFECTS_SOURCE.read_text(encoding="utf-8"))
    assert declared and declared.group(1) == row["CataclysmType"], (
        "HolyRepercussionsResistance in CataclysmDungeonModifierEffects.cpp is "
        f"{declared.group(1) if declared else 'not declared'!r}, which is not "
        f"the row's own type {row['CataclysmType']!r}.")


def test_holy_repercussions_chance_is_still_the_tables_figure_for_an_event():
    """The one judgement with a precedent in the table, held to that precedent.

    Ten is what `game/Data/DungeonModifiers.csv` uses for a chance fired by an
    event, and `SporeCloudsChancePercentOnDeath` took it for exactly that reason.
    This row's chance is also fired by an event, so it follows the same figure.

    NOT A COMPARISON OF A CONSTANT WITH ITSELF. Both are written as literals,
    independently, so the two can drift apart -- which is what this notices.
    `IllusoryEnemiesSharePercent` is deliberately NOT this figure, because a share
    of a floor's population is not a chance fired by an event.
    """
    holy = constant("HolyRepercussionsChancePercentOnHit")
    spores = constant("SporeCloudsChancePercentOnDeath")

    assert holy == spores, (
        f"HolyRepercussionsChancePercentOnHit is {holy:g} and "
        f"SporeCloudsChancePercentOnDeath is {spores:g}. Both are a chance fired "
        "by an event and both followed the table's figure for one; if one has "
        "moved on purpose, say why beside it and in docs/DECISIONS.md.")


def test_judgment_is_written_to_one_resistance_outside_the_all_resistance_loop():
    """The structural decision, held in the source.

    `StatModifiersFor` applies `ResistanceLessPercent` and `ResistanceMorePercent`
    inside a loop over every damage type. Judgment moves ONE resistance, which is
    why it got its own field rather than sharing theirs. If its write moved inside
    that loop it would lower all eight, and the automation test that counts
    exactly one moved stat would be the only thing to notice.

    THE USE, NOT THE NAME. A comment could name the field inside the loop and
    satisfy a check for the bare word, so this looks for the field being READ:
    `Effects.JudgmentResistanceLessPercent`.
    """
    source = EFFECTS_SOURCE.read_text(encoding="utf-8")
    function = body_of(
        source,
        "TMap<FName, TArray<FCataclysmStatModifier>> "
        "UCataclysmDungeonModifierEffects::StatModifiersFor(")
    loop = body_of(
        function,
        "for (const FName DamageType : UCataclysmItemModifiers::DamageTypeNames())")

    use = "Effects.JudgmentResistanceLessPercent"
    assert use in function, (
        "StatModifiersFor no longer reads Effects.JudgmentResistanceLessPercent, "
        "so Judgment reaches no stat at all.")
    assert use not in loop, (
        "StatModifiersFor now reads Effects.JudgmentResistanceLessPercent INSIDE "
        "the loop over every damage type. Judgment lowers one resistance; inside "
        "that loop it lowers all eight. See JudgmentResistanceLessPercent in "
        "CataclysmDungeonModifierEffects.h for why it has its own field.")


def test_leech_spores_row_still_states_the_heal_radius_the_rule_uses():
    """The one figure the row states, held to the words it came from.

    "any enemies within a 10-meter radius". `LeechSporesHealRadiusCm` is read off
    that sentence, so if the row's figure moves the constant has to move with it.
    Nothing in C++ would notice: the automation tests build every distance from
    the constant, so they agree with themselves at any value.
    """
    words = flat(rows()["Pestilence_Leech_Spores"]["Description"])
    metres = constant("LeechSporesHealRadiusCm") / 100.0

    assert f"{metres:g}-meter radius" in words, (
        f"Pestilence_Leech_Spores no longer says '{metres:g}-meter radius'. "
        "LeechSporesHealRadiusCm in CataclysmDungeonModifierEffects.h is read off "
        "that sentence, so move it to whatever the row now says. " + words)


def test_leech_spores_row_still_says_a_kill_leaves_a_cloud_from_the_corpse():
    """The trigger, and why the floor owns the cloud rather than the creature.

    "When you kill an enemy" is why a listener sits on the death announcement.
    "from their corpse" is why the floor hazard source owns the cloud: a hazard
    left by a creature that is already dead cannot be owned by it, because
    `ACataclysmGroundZone::Sweep` returns early when its owner is gone.
    """
    words = flat(rows()["Pestilence_Leech_Spores"]["Description"]).lower()

    assert "when you kill an enemy" in words, (
        "Pestilence_Leech_Spores no longer says a cloud comes from killing an "
        "enemy. NoteDeathForLeechSpores listens to the death announcement because "
        "of those words. " + words)
    assert "from their corpse" in words, (
        "Pestilence_Leech_Spores no longer says the cloud comes from the corpse. "
        "The floor hazard source owns the cloud because a dead creature cannot; "
        "if the cloud no longer comes from a death, that reasoning has to be made "
        "again. " + words)


def test_leech_spores_row_still_says_the_drain_heals_enemies():
    """What contact does, in the row's own words.

    "a small portion of your health is drained and used to heal any enemies".
    "a small portion" is why the drain share is a judgement rather than a figure
    read off the row; "drained" is why the health is TAKEN rather than dealt as a
    blow; "used to heal" is why the heal is paid from what left the player.
    """
    words = flat(rows()["Pestilence_Leech_Spores"]["Description"]).lower()

    for phrase in ("a small portion of your health", "is drained",
                   "used to heal any enemies"):
        assert phrase in words, (
            f"Pestilence_Leech_Spores no longer says {phrase!r}. The drain's share, "
            "its being taken rather than dealt, and the heal being paid from it all "
            "rest on that sentence; see LeechSporesKey in "
            "CataclysmDungeonModifierEffects.h. " + words)


def test_leech_spores_leaves_a_cloud_only_for_the_players_kills():
    """The row's "When you kill an enemy", held to the comparison that reads it.

    The Mortal Decay entry in docs/DECISIONS.md records that a row naming who
    does the killing makes its death listener require
    `FCataclysmDeathNotice::Killer` to be the player's pawn. Most death listeners
    on the game mode do not ask, because their rows name no killer, so a change
    making this one look like its neighbours would read as tidying rather than
    as a change to the rule. This fails first.

    THE COMPARISON, NOT THE NAME. Comment lines are dropped before searching,
    because the comment above the check names the killer in prose.
    """
    source = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
              / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    body = body_of(source, "void ACataclysmDungeonGameMode::NoteDeathForLeechSpores(")
    code = "\n".join(line for line in body.splitlines()
                     if not line.lstrip().startswith("//"))

    assert re.search(r"Notice\.Killer\s*!=\s*Player\b", code), (
        "NoteDeathForLeechSpores no longer refuses a death the player did not "
        "cause. Pestilence_Leech_Spores says \"When you kill an enemy\", and the "
        "Mortal Decay entry in docs/DECISIONS.md records that a row naming the "
        "killer counts only the player's kills. Without the check a creature "
        "killed by anything else leaves a cloud.")


def test_leech_spores_drains_rather_than_dealing_a_blow():
    """The decision that decides how this rule meets every rule listening for blows.

    `StepLeechSpores` takes the player's health through
    `UCataclysmSkillEffects::ReduceHealthDirectly`, which is NOT announced as a hit.
    A floor also carrying `Celestial_Holy_Repercussions` or
    `Demonic_Brand_of_the_Aggressor` -- both of which listen for blows -- therefore
    does not count a drain as one. Dealing it through `ApplyDirectDamage` or
    `ApplyHit` instead would announce it, and would also let armour reduce what
    leaves the player, so the heal would no longer be paid from a drain.

    THE CALL, NOT THE NAME. Comments in that function name `ReduceHealthDirectly`
    in prose, so this looks for the qualified call with its opening bracket, which
    a comment does not write.
    """
    source = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
              / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    body = body_of(source, "void ACataclysmDungeonGameMode::StepLeechSpores(")

    assert "UCataclysmSkillEffects::ReduceHealthDirectly(" in body, (
        "StepLeechSpores no longer drains the player through "
        "UCataclysmSkillEffects::ReduceHealthDirectly. That call is not announced as "
        "a hit and no armour reduces it; without it the drain is a blow that other "
        "rules count and armour shrinks.")
    for blow in ("UCataclysmSkillEffects::ApplyDirectDamage(",
                 "UCataclysmSkillEffects::ApplyHit("):
        assert blow not in body, (
            f"StepLeechSpores now calls {blow}. The row says the health is "
            "DRAINED; a blow is announced to every rule listening for blows and is "
            "reduced by armour, so the heal would no longer be paid from a drain.")


def test_blood_altar_row_still_names_no_killer():
    """The row's "Slaying enemies" names nobody, which is why every death feeds the altar.

    `NoteDeathForBloodAltar` asks only that the victim is a creature. The Mortal Decay
    entry in docs/DECISIONS.md records that a row naming who does the killing counts
    only the player's kills, which is what Leech Spores does for "When you kill an
    enemy". If this row came to name the player as the killer, the listener would have
    to ask too.
    """
    words = flat(rows()["Demonic_Blood_Altar"]["Description"]).lower()

    assert "slaying enemies contributes to the blood altar" in words, (
        "Demonic_Blood_Altar no longer says 'Slaying enemies contributes to the blood "
        "altar'. NoteDeathForBloodAltar counts every creature death because of those "
        "words. " + words)
    named = re.search(r"\byou (kill|slay)\b|\bthe player (kills|slays)\b", words)
    assert not named, (
        f"Demonic_Blood_Altar now says {named.group(0) if named else ''!r}, which "
        "names who does the killing. The Mortal Decay entry in docs/DECISIONS.md "
        "records that such a row counts only the player's kills, so "
        "NoteDeathForBloodAltar would have to ask who killed, as "
        "NoteDeathForLeechSpores does. " + words)


def test_blood_altar_row_still_says_the_pulses_grow_with_the_deaths():
    """The one relation the row states, held to the words it came from.

    "The altar sends out damaging pulses that grow stronger with the number of enemies
    slain." `BloodAltarPulseDamage` takes a share for each death counted because of
    those words. Every figure in it is a judgement or the owner's, recorded in
    docs/DECISIONS.md, and none is read off the row.
    """
    words = flat(rows()["Demonic_Blood_Altar"]["Description"]).lower()

    for phrase in ("sends out damaging pulses",
                   "grow stronger with the number of enemies slain"):
        assert phrase in words, (
            f"Demonic_Blood_Altar no longer says {phrase!r}. BloodAltarPulseDamage and "
            "StepBloodAltar rest on that sentence; see BloodAltarKey in "
            "CataclysmDungeonModifierEffects.h. " + words)


def _game_mode_code_of(opening: str) -> str:
    """A game mode function's body with its comment lines removed."""
    source = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
              / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    return "\n".join(line for line in body_of(source, opening).splitlines()
                     if not line.lstrip().startswith("//"))


def test_blood_altar_counts_deaths_without_asking_who_killed():
    """The decision that makes this row the opposite of Leech Spores, held in the code.

    Two death listeners beside this one ask who killed -- Leech Spores and Mortal
    Decay -- so copying a neighbour could add the question here by accident. This
    fails if it does.

    THE CODE, NOT THE COMMENTS. Comment lines are removed first, because this
    function's comments say in words that the killer is not asked about. And the
    victim is looked for too, so an absence of the killer cannot pass merely because
    the function stopped reading the notice.
    """
    code = _game_mode_code_of("void ACataclysmDungeonGameMode::NoteDeathForBloodAltar(")

    assert "Notice.Victim" in code, (
        "NoteDeathForBloodAltar no longer reads the notice's victim at all, so this "
        "check cannot tell a listener that ignores the killer from one that reads "
        "nothing. " + code)
    assert "Killer" not in code, (
        "NoteDeathForBloodAltar now reads the notice's killer. Demonic_Blood_Altar "
        "says 'Slaying enemies', which names nobody, so every creature death feeds the "
        "altar; docs/DECISIONS.md records that as the opposite of Leech Spores.")


def test_no_floor_rule_writes_a_shared_damage_type():
    """A floor's damage type travels with its hit or its zone, never on the source (#1924).

    Every dungeon floor rule deals damage in the name of one actor,
    `ACataclysmFloorHazardSource`, and all the rules on a floor share it. Until issue
    #1924 it held a `DamageType` that rules wrote before dealing damage, and every hit the
    floor dealt was typed by it. So the type was whichever rule had written it last: on a
    floor of two Cataclysms one rule's damage met the other's resistance, and a rule that
    never wrote it dealt damage that no resistance met.

    THE HEADER IS CHECKED BECAUSE THE COMPILER REFUSES A WRITE ONLY WHILE THE FIELD IS
    GONE. The game mode is checked too, because a write through a pointer is how the rules
    set the field, and it would compile again the day the field came back.

    THE CODE, NOT THE COMMENTS. Both files explain the removed field in comments, and
    those lines are removed before the search. Each file is also checked for something it
    must still contain, so a file that was not read cannot pass.
    """
    dungeon = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"

    def code_lines(path: pathlib.Path) -> str:
        return "\n".join(
            line for line in path.read_text(encoding="utf-8").splitlines()
            if not line.lstrip().startswith(("//", "/*", "*")))

    header = code_lines(dungeon / "CataclysmFloorHazardSource.h")
    assert "static ACataclysmFloorHazardSource* ForFloor(" in header, (
        "CataclysmFloorHazardSource.h no longer declares ForFloor, so this check cannot "
        "tell a header with no type field from one it did not read. " + header)
    assert "DamageType" not in header, (
        "ACataclysmFloorHazardSource declares a DamageType again. Every rule on a floor "
        "shares that actor, so a type held on it is whichever rule wrote it last. Issue "
        "#1924 moved the type onto FCataclysmHitDelivery and ACataclysmGroundZone. "
        + header)

    game_mode = code_lines(dungeon / "CataclysmDungeonGameMode.cpp")
    assert "Delivery.DamageType = " in game_mode, (
        "CataclysmDungeonGameMode.cpp no longer puts a type on any hit's delivery, so this "
        "check cannot tell a file that writes no shared type from one it did not read.")
    written = re.findall(r"^.*->\s*DamageType\s*=.*$", game_mode, flags=re.MULTILINE)
    assert not written, (
        "CataclysmDungeonGameMode.cpp writes a damage type through a pointer, which is how "
        "the rules wrote the floor source's shared type before issue #1924. A floor rule's "
        "damage carries its row's type on its hit's delivery or on its zone instead: "
        + "; ".join(line.strip() for line in written))


def test_blood_altar_passes_its_rows_type_on_the_pulse():
    """The pulse carries its row's type on its own delivery, set before it is dealt (#1924).

    `StepBloodAltar` deals the pulse from the floor's hazard source, which holds no type
    since issue #1924. Without the type on the delivery the pulse meets no resistance, and
    a type set after the pulse is dealt types nothing.

    THE CODE, NOT THE COMMENTS, as in the check above on this rule's death listener.
    """
    code = _game_mode_code_of("void ACataclysmDungeonGameMode::StepBloodAltar(")

    typed = code.find("Delivery.DamageType = FName(*Row->CataclysmType);")
    deals = code.find("UCataclysmSkillEffects::ApplyDirectDamage(")

    assert typed != -1, (
        "StepBloodAltar no longer puts its own row's type on the pulse's delivery, so the "
        "pulse is dealt untyped and no resistance meets it. Issue #1924. " + code)
    assert deals != -1, (
        "StepBloodAltar no longer deals its pulse through ApplyDirectDamage, which is the "
        "route docs/DECISIONS.md records for it.")
    assert typed < deals, (
        "StepBloodAltar puts the type on the pulse's delivery after dealing the pulse, so "
        "the pulse is dealt untyped.")


def test_every_ground_zone_the_game_mode_places_is_owned_by_the_hazard_source():
    """A same-arena floor change destroys the rules' zones by their owner (#1925).

    `DungeonGameModeDestroyTheRulesZones` in `CataclysmDungeonGameMode.cpp` destroys every
    ground zone owned by the floor's `ACataclysmFloorHazardSource`, at the top of
    `ApplyFloorRulesToPlayer`. That finds a rule's zones only if the rule placed them in
    the source's name. A zone placed with any other owner would stay on a Horde
    dungeon's next wave with no rule acting for it, and nothing in C++ would say so.

    TWO THINGS, BECAUSE A NAME IS NOT A TYPE: every zone spawn in the file passes a
    variable named `Source` as its owner, and every function holding such a spawn takes
    `Source` from `ACataclysmFloorHazardSource::ForFloor`.

    THE CODE, NOT THE COMMENTS. Comment lines are removed first; the file explains in
    comments how its zones are placed.
    """
    path = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
            / "CataclysmDungeonGameMode.cpp")
    code = "\n".join(
        line for line in path.read_text(encoding="utf-8").splitlines()
        if not line.lstrip().startswith(("//", "/*", "*")))

    spawns = list(re.finditer(
        r"ACataclysmGroundZone::(Spawn|SpawnAlong|SpawnForTheFloor)\(\s*(\w+)\s*,", code))
    assert spawns, (
        "CataclysmDungeonGameMode.cpp places no ground zone that this check can find, so it "
        "would pass having checked nothing. Were the spawn calls renamed?")

    definitions = [(found.start(), found.group(1)) for found in re.finditer(
        r"^\w[^\n;]*\bACataclysmDungeonGameMode::(\w+)\(", code, flags=re.MULTILINE)]
    declared = "ACataclysmFloorHazardSource* Source = ACataclysmFloorHazardSource::ForFloor("

    wrong_owner = []
    not_the_source = []
    for spawn in spawns:
        enclosing = [(at, name) for at, name in definitions if at < spawn.start()]
        assert enclosing, (
            f"A zone spawn at character {spawn.start()} of CataclysmDungeonGameMode.cpp is "
            "outside every ACataclysmDungeonGameMode function, which this check does not "
            "expect.")
        at, name = enclosing[-1]
        if spawn.group(2) != "Source":
            wrong_owner.append(f"{name} passes {spawn.group(2)}")
        if declared not in code[at:spawn.start()]:
            not_the_source.append(name)

    assert not wrong_owner, (
        "A dungeon rule places a ground zone with an owner other than the floor's hazard "
        "source, so a Horde dungeon's next wave would keep that zone: "
        + "; ".join(wrong_owner) + f". Read {len(spawns)} zone spawns.")
    assert not not_the_source, (
        "A dungeon rule passes a Source that does not come from "
        "ACataclysmFloorHazardSource::ForFloor in the same function: "
        + "; ".join(not_the_source) + f". Read {len(spawns)} zone spawns.")


def test_brand_of_the_aggressor_row_still_states_the_count_the_rule_uses():
    """The first dungeon rule whose count is READ OFF THE ROW rather than judged.

    `Pestilence_Spore_Clouds` and `Demonic_Hellfire` both had to answer a
    "chance" their rows left open. This row states its own: "At 20 stacks, you
    erupt". So the constant is a reading, and this is what holds the reading to
    the thing read.

    IF THE ROW'S COUNT MOVES, THE RULE'S MUST. Nothing in C++ would notice: the
    automation tests build every expectation from the constant, so they would
    agree with themselves at any value.
    """
    words = flat(rows()["Demonic_Brand_of_the_Aggressor"]["Description"])
    stacks = whole_number("BrandStacksToErupt")

    assert f"At {stacks} stacks" in words, (
        f"The row no longer says 'At {stacks} stacks'. BrandStacksToErupt in "
        "CataclysmDungeonModifierEffects.h is read off this sentence, so move it "
        "to whatever the row now says. " + words)


def test_brand_of_the_aggressor_row_still_states_the_share_the_nova_deals():
    """The other figure the row states, held the same way.

    "deals 20% of your max HP". DEALT AND NOT TAKEN: the nova goes through the
    ordinary area-blow delivery, so armour and resistances take their cut and
    what reaches health is less. The row says what is dealt and the rule deals it.
    """
    words = flat(rows()["Demonic_Brand_of_the_Aggressor"]["Description"])
    share = constant("BrandNovaMaxHealthPercent")

    assert f"deals {share:g}% of your max HP" in words, (
        f"The row no longer says 'deals {share:g}% of your max HP'. "
        "BrandNovaMaxHealthPercent in CataclysmDungeonModifierEffects.h is read "
        "off this sentence. " + words)


def test_brand_of_the_aggressor_row_still_names_the_players_own_blow():
    """Which direction the rule reads the blow announcement in.

    THE LISTENER TESTS `Notice.Attacker` WHERE WASTING SICKNESS TESTS
    `Notice.Target`, and the only reason is this sentence: "Hitting an enemy
    applies a stack ... to you". A row that moved to being hit would leave the
    rule reading the announcement backwards, and nothing in C++ would say so.
    """
    words = flat(rows()["Demonic_Brand_of_the_Aggressor"]["Description"]).lower()

    assert "hitting an enemy" in words, words
    assert "to you" in words, words


def test_brand_of_the_aggressor_row_still_aims_the_nova_at_the_players_side():
    """Who the nova reaches, which is a different answer from the row beside it.

    "to you and nearby allies". `Demonic_Hellfire` names nobody and therefore
    catches everyone, through `FindEveryoneInLine`; this row names its two sides,
    so the rule damages the player and asks `FindAlliesInSphere` for the rest.

    IF THIS ROW EVER NAMES ENEMIES, the rule is asking the wrong search and this
    fails first.
    """
    words = flat(rows()["Demonic_Brand_of_the_Aggressor"]["Description"]).lower()

    assert "to you and nearby allies" in words, words
    assert "enemies" not in words, (
        "Demonic_Brand_of_the_Aggressor now names enemies. Its nova asks "
        "FindAlliesInSphere because the row named only the player's own side -- "
        "see NoteHitForBrandOfTheAggressor in CataclysmDungeonGameMode.h. " + words)


def test_brand_of_the_aggressor_nova_radius_is_still_a_derivation():
    """The one figure the row does not state, held as a derivation not a number.

    300 IS THIS PROJECT'S SETTLED ANSWER for a thing at a point on the floor, and
    the nova's radius is declared as `InfernalRainRadiusCm` so a later change to
    one carries the other. This is the same shape as
    `test_hellfire_reach_is_still_declared_as_the_floors_own_radius` above.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    declared = re.search(
        r"BrandNovaRadiusCm\s*=\s*InfernalRainRadiusCm\s*;", text)

    assert declared, (
        "Brand of the Aggressor's nova radius is no longer declared as "
        "InfernalRainRadiusCm. It was copied as a conclusion because 300 is what "
        "this project uses for a thing at a point on the floor. If it is now a "
        "figure of its own, say why in docs/DECISIONS.md.")


def test_necrotic_ground_row_still_states_the_healing_cut_the_rule_uses():
    """The fog's first figure is READ OFF THE ROW, not judged.

    "reduces your healing effectiveness by 50% while standing in it".
    `NecroticGroundHealingLessPercent` is that 50, written while the player stands in a
    patch into the stat Death's Embrace also writes.

    IF THE ROW'S NUMBER MOVES, THE RULE'S MUST. The automation tests build their
    expectation from the constant, so they would agree with themselves at any value, and
    the floor panel would show the player the row's figure while the game cut another.
    """
    words = flat(rows()["Death_Necrotic_Ground"]["Description"])
    cut = constant("NecroticGroundHealingLessPercent")

    expected = f"reduces your healing effectiveness by {cut:g}% while standing in it"
    assert expected in words, (
        f"The row no longer says '{expected}'. NecroticGroundHealingLessPercent in "
        "CataclysmDungeonModifierEffects.h is read off this sentence, so move it to "
        "whatever the row now says. " + words)


def test_necrotic_ground_row_still_states_the_regeneration_the_rule_uses():
    """The fog's second figure, held the same way: "regen their health at 10%/s".

    A SHARE OF EACH CREATURE'S OWN MAXIMUM, A SECOND. The rule pays a quarter of it on
    each quarter-second beat, so the constant is per second only while the row says "/s".
    """
    words = flat(rows()["Death_Necrotic_Ground"]["Description"])
    regen = constant("NecroticGroundCreatureRegenPercentPerSecond")

    expected = f"Enemies standing in the fog regen their health at {regen:g}%/s"
    assert expected in words, (
        f"The row no longer says '{expected}'. "
        "NecroticGroundCreatureRegenPercentPerSecond in "
        "CataclysmDungeonModifierEffects.h is read off this sentence, so move it to "
        "whatever the row now says. " + words)


def test_necrotic_ground_row_still_says_a_spreading_fog_that_hurts_and_no_third_figure():
    """The words the rule's shape is read from, and the figures it was not given.

    "A SPREADING NECROTIC FOG" is why patches keep appearing, each touching one already
    there, up to a cap. "DEALS DAMAGE OVER TIME" is why the fog burns once a second
    rather than once on entering.

    THE ROW STATES TWO FIGURES AND NO THIRD. The cadence, the cap, the patch size and the
    burn's share are judgements in docs/DECISIONS.md. If the row ever states one of them,
    this fails, so the constant is read off the row instead and the entry stops calling
    it a judgement.
    """
    words = flat(rows()["Death_Necrotic_Ground"]["Description"])
    lowered = words.lower()
    stated = re.findall(r"[0-9]+(?:\.[0-9]+)?", words)
    read = [f"{constant('NecroticGroundHealingLessPercent'):g}",
            f"{constant('NecroticGroundCreatureRegenPercentPerSecond'):g}"]

    assert "a spreading necrotic fog" in lowered, (
        "The Necrotic Ground row no longer says the fog spreads. The rule places a new "
        "patch every NecroticGroundSecondsBetweenPatches because it does; re-read the "
        "ruling in docs/DECISIONS.md. " + words)
    assert "deals damage over time" in lowered, (
        "The Necrotic Ground row no longer says the fog deals damage over time. The rule "
        "burns a player standing in it once a second because it does; re-read the ruling "
        "in docs/DECISIONS.md. " + words)
    assert stated == read, (
        f"The Necrotic Ground row states the figures {stated}, and the rule reads only "
        f"{read} off it. A new figure is one of the judgements docs/DECISIONS.md records "
        "for this rule: read it off the row instead. " + words)


def test_necrotic_grounds_patch_size_first_reach_and_burn_are_still_derivations():
    """Three figures the row does not state, held as derivations and not as numbers.

    EACH WAS COPIED FROM A RULE THAT ANSWERED THE SAME QUESTION, so a later change to
    that rule carries this one:

    - a patch is as wide as Infernal Rain's, `InfernalRainRadiusCm`, the figure this
      project uses for a thing at a point on the floor;
    - the first patch lands where Infernal Rain's does, within `InfernalRainFallsWithinCm`
      of the player and never on them;
    - the burn is Singularity Wells' share, `SingularityWellsPercentPerSecond`, the lower
      of the two shares the floor's burning zones take, because the fog also halves
      healing.

    REPLACING ANY OF THEM WITH A LITERAL WOULD BREAK THAT LINK SILENTLY, which is what this
    notices. The same shape as
    `test_brand_of_the_aggressor_nova_radius_is_still_a_derivation` above.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    derivations = {
        "NecroticGroundPatchRadiusCm": "InfernalRainRadiusCm",
        "NecroticGroundFirstPatchWithinCm": "InfernalRainFallsWithinCm",
        "NecroticGroundPercentPerSecond": "SingularityWellsPercentPerSecond",
    }
    lost = [f"{name} is no longer declared as {source}"
            for name, source in derivations.items()
            if not re.search(rf"\b{name}\s*=\s*{source}\s*;", text)]

    assert not lost, (
        "; ".join(lost) + ". Each was copied from that rule as a conclusion, not "
        "chosen as a number. If one is now a figure of its own, say why in "
        "docs/DECISIONS.md.")


def test_ravenous_hoard_row_states_no_number_of_its_own():
    """Every figure this rule uses is judged: its row states none.

    IF THE ROW EVER STATES ONE, this fails, so the constant is read off the row and
    docs/DECISIONS.md stops calling it a judgement. The same shape as
    `test_grasping_tentacles_states_no_number_of_its_own` above.
    """
    words = flat(rows()["Famine_Ravenous_Hoard"]["Description"])

    assert "%" not in words, words
    assert not [c for c in words if c.isdigit()], (
        "The Ravenous Hoard row now states a number. Check "
        "RavenousHoardSecondsPerStack, RavenousHoardMostStacks and "
        "RavenousHoardDamagePercentPerStack against it and update docs/DECISIONS.md. "
        + words)


def test_ravenous_hoard_row_still_says_enemies_grow_stronger_the_longer_they_remain_alive():
    """The two phrases the rule's readings rest on.

    "ENEMIES IN THE DUNGEON" NAMES NO EXCEPTION, which is why bosses and creatures
    placed later grow too. "GROW STRONGER THE LONGER THEY REMAIN ALIVE" is why each
    creature counts its own time alive rather than the floor's.
    """
    words = flat(rows()["Famine_Ravenous_Hoard"]["Description"]).lower()

    assert "enemies in the dungeon" in words, (
        "The Ravenous Hoard row no longer names enemies in the dungeon without "
        "exception. The rule grows every hostile creature, bosses included, because "
        "it did; re-read the ruling in docs/DECISIONS.md. " + words)
    assert "grow stronger the longer they remain alive" in words, (
        "The Ravenous Hoard row no longer says enemies grow stronger the longer they "
        "remain alive. Each creature counts its own time alive because it did; re-read "
        "the ruling in docs/DECISIONS.md. " + words)


def test_ravenous_hoards_cadence_cap_and_share_are_still_death_s_embrace_s():
    """The three judged figures, held as derivations and not as numbers.

    DEATH'S EMBRACE IS THIS PROJECT'S ANSWER TO "WORSE THE LONGER YOU STAY", on the
    player's side: a stack every ten seconds, ten each, five at most. Ravenous Hoard
    declares all three as that rule's constants, so a later change to one carries the
    other. The same shape as
    `test_necrotic_grounds_patch_size_first_reach_and_burn_are_still_derivations` above.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    derivations = {
        "RavenousHoardSecondsPerStack": "DeathsEmbraceSecondsPerStack",
        "RavenousHoardMostStacks": "DeathsEmbraceMostStacks",
        "RavenousHoardDamagePercentPerStack": "DeathsEmbracePercentPerStack",
    }
    lost = [f"{name} is no longer declared as {source}"
            for name, source in derivations.items()
            if not re.search(rf"\b{name}\s*=\s*{source}\s*;", text)]

    assert not lost, (
        "; ".join(lost) + ". Each was copied from Death's Embrace as a conclusion, "
        "not chosen as a number. If one is now a figure of its own, say why in "
        "docs/DECISIONS.md.")


def test_grave_tide_row_states_no_number_of_its_own():
    """Every figure this rule uses is judged: its row states none.

    IF THE ROW EVER STATES ONE -- a count of creatures, a cadence, a share -- this
    fails, so the constant is read off the row and docs/DECISIONS.md stops calling it a
    judgement.
    """
    words = flat(rows()["Death_Grave_Tide"]["Description"])

    assert "%" not in words, words
    assert not [c for c in words if c.isdigit()], (
        "The Grave Tide row now states a number. Check GraveTideSecondsBetweenWaves, "
        "GraveTideFirstWaveCreatures, GraveTideMoreCreaturesPerWave, GraveTideMostWaves "
        "and GraveTideDamagePercentPerWave against it and update docs/DECISIONS.md. "
        + words)


def test_grave_tide_row_still_says_waves_rise_periodically_and_grow():
    """The three phrases the rule's readings rest on.

    "PERIODICALLY" is why the waves come on a clock rather than on a trigger. "GROW
    STRONGER AND MORE NUMEROUS" is why each wave is larger than the last and its
    creatures are placed above their own damage. "THE LONGER PLAYERS REMAIN ON A FLOOR"
    is why both are counted from the floor's start and cleared at the stairs.
    """
    words = flat(rows()["Death_Grave_Tide"]["Description"]).lower()

    assert "periodically" in words, (
        "The Grave Tide row no longer says the waves come periodically. The rule counts "
        "a cadence because it did; re-read the ruling in docs/DECISIONS.md. " + words)
    assert "grow stronger and more numerous" in words, (
        "The Grave Tide row no longer says the waves grow stronger and more numerous. "
        "Each wave is larger than the last and its creatures are placed above their own "
        "damage because it did. " + words)
    assert "the longer players remain on a floor" in words, (
        "The Grave Tide row no longer counts the growth by time on a floor. The rule's "
        "clock and its count of waves are cleared at the stairs because it did. " + words)


def test_grave_tides_cadence_and_shares_are_still_the_rules_they_came_from():
    """The judged figures that were copied from other rules, held as derivations.

    THE CADENCE IS THE ARTILLERY STRIKE'S, this project's figure for a floor event on a
    clock. THE SHARE A WAVE ADDS IS DEATH'S EMBRACE'S, the same step Ravenous Hoard
    gives for time alive, AND THE CEILING IS RAVENOUS HOARD'S CAP AT THAT SHARE, so a
    wave cannot place a creature stronger than one that lived to that rule's cap. A
    later change to any of those carries this rule with it.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    derivations = {
        "GraveTideSecondsBetweenWaves": r"ArtilleryStrikeSecondsBetween",
        "GraveTideDamagePercentPerWave": r"DeathsEmbracePercentPerStack",
        "GraveTideMostDamagePercent":
            r"RavenousHoardMostStacks\s*\*\s*GraveTideDamagePercentPerWave",
    }
    lost = [f"{name} is no longer declared as {source}"
            for name, source in derivations.items()
            if not re.search(rf"\b{name}\s*=\s*{source}\s*;", text)]

    assert not lost, (
        "; ".join(lost) + ". Each was copied from another rule as a conclusion, not "
        "chosen as a number. If one is now a figure of its own, say why in "
        "docs/DECISIONS.md.")


def test_volatile_evolution_row_still_states_the_threshold_the_header_uses():
    """The one figure this rule does not judge: the row states it.

    "ONCE THEY DROP BELOW 75% HP". Unlike the rows either side of it, this one carries a
    number, so the constant is read off the row rather than ruled. If the row's figure
    moves and the header's does not, the rule stops being the row.
    """
    words = flat(rows()["Chaos_Volatile_Evolution"]["Description"])
    stated = re.findall(r"(\d+(?:\.\d+)?)\s*%", words)

    assert stated == ["75"], (
        "The Volatile Evolution row no longer states 75% as the health a creature "
        "mutates below. Check VolatileEvolutionHealthPercentToMutate against it and "
        "update docs/DECISIONS.md. " + words)

    text = EFFECTS_HEADER.read_text(encoding="utf-8")
    assert re.search(
        rf"\bVolatileEvolutionHealthPercentToMutate\s*=\s*{stated[0]}\.0f\s*;", text), (
        f"The row says {stated[0]}% and VolatileEvolutionHealthPercentToMutate no longer "
        "says the same. One of the two moved without the other.")


def test_volatile_evolution_row_still_says_a_chance_to_become_a_higher_rarity():
    """The three phrases the rule's readings rest on.

    "A CHANCE" is why a beat rolls rather than mutating every wounded creature. "MUTATE
    INTO HIGHER RARITY MOBS" is why the rule moves the rung of the rarity ladder, which
    is what carries the bigger stat block, the extra modifier and the better drop. "ONCE
    THEY DROP BELOW" is why a wound is the trigger and not a hit, a death or a clock.
    """
    words = flat(rows()["Chaos_Volatile_Evolution"]["Description"]).lower()

    assert "a chance" in words, (
        "The Volatile Evolution row no longer says enemies have a chance. The rule rolls "
        "against VolatileEvolutionChancePercent because it did; re-read the ruling in "
        "docs/DECISIONS.md. " + words)
    assert "higher rarity" in words, (
        "The Volatile Evolution row no longer says the enemy becomes a higher rarity. "
        "The rule raises the rung of the rarity ladder because it did, which is also "
        "what makes the creature worth more when it dies. " + words)
    assert "once they drop below" in words, (
        "The Volatile Evolution row no longer makes a wound the trigger. The rule tests "
        "health on every beat because it did. " + words)


def test_volatile_evolutions_chance_is_its_own_number_and_not_another_rules():
    """The chance is a judgement of its own, deliberately not bound to a neighbour.

    THREE OTHER ROWS IN THIS LIBRARY SAY "A CHANCE" AND ALL THREE USE TEN, which is
    where this figure started. Writing it as `= SporeCloudsChancePercentOnDeath` would
    say the two must move together, and the ruling did not say that. This check fails if
    somebody later binds them, so the decision is made again rather than by accident.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    assert re.search(r"\bVolatileEvolutionChancePercent\s*=\s*\d+(?:\.\d+)?f\s*;", text), (
        "VolatileEvolutionChancePercent is no longer a number of its own. If it is now "
        "another rule's figure, say why in docs/DECISIONS.md.")
    assert re.search(r"\bVolatileEvolutionRungsGained\s*=\s*\d+\s*;", text), (
        "VolatileEvolutionRungsGained is no longer a number of its own.")


def test_volatile_evolutions_ceiling_is_tied_to_the_first_boss_rung():
    """The ceiling on a mutation is the rung under the first boss rung, and says so.

    THE TWO CONSTANTS LIVE IN DIFFERENT FILES, so nothing but this tie stops a ladder
    that gains or loses a rung from letting a floor rule make a boss out of an ordinary
    creature in the middle of a fight. The tie is a static_assert in the game mode, where
    the ceiling is applied; this check is that it is still there, because continuous
    integration builds no C++ and would not notice it going.
    """
    header = EFFECTS_HEADER.read_text(encoding="utf-8")
    assert re.search(r"\bVolatileEvolutionHighestRung\s*=\s*\d+\s*;", header), (
        "VolatileEvolutionHighestRung is no longer a plain rung number in "
        "CataclysmDungeonModifierEffects.h.")

    mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    tie = re.search(
        r"static_assert\(\s*UCataclysmDungeonModifierEffects::VolatileEvolutionHighestRung"
        r"\s*==\s*ACataclysmEnemyCharacter::FirstBossRarityStep\s*-\s*1\s*,", mode)

    assert tie, (
        "The static_assert tying Volatile Evolution's ceiling to "
        "ACataclysmEnemyCharacter::FirstBossRarityStep - 1 is gone from "
        "CataclysmDungeonGameMode.cpp. Without it the ceiling is a bare 3 that a change "
        "to the rarity ladder would silently make wrong.")


def test_royal_guard_row_still_states_the_three_figures_the_rule_uses():
    """Three of this rule's four numbers are the row's own, not judgements.

    "DROPS BELOW 30% HEALTH", "A 50% CHANCE" and "TWO GUARDS". If any of them moves in the
    row and not in the header, the rule stops being the row, and docs/DECISIONS.md stops
    being able to call only the fourth a judgement.
    """
    words = flat(rows()["War_Royal_Guard"]["Description"])
    stated = re.findall(r"(\d+(?:\.\d+)?)\s*%", words)

    assert stated == ["30", "50"], (
        "The Royal Guard row no longer states 30% health and a 50% chance in that order. "
        "Check RoyalGuardHealthPercentToSummon and RoyalGuardChancePercent against it and "
        "update docs/DECISIONS.md. " + words)
    assert "two guards" in words.lower(), (
        "The Royal Guard row no longer says two guards arrive. Check "
        "RoyalGuardGuardsSummoned against it. " + words)

    text = EFFECTS_HEADER.read_text(encoding="utf-8")
    missing = [name for name, pattern in (
        ("RoyalGuardHealthPercentToSummon", r"RoyalGuardHealthPercentToSummon\s*=\s*30\.0f\s*;"),
        ("RoyalGuardChancePercent", r"RoyalGuardChancePercent\s*=\s*50\.0f\s*;"),
        ("RoyalGuardGuardsSummoned", r"RoyalGuardGuardsSummoned\s*=\s*2\s*;"),
    ) if not re.search(pattern, text)]

    assert not missing, (
        f"{', '.join(missing)} no longer holds the figure the row states. The row says "
        f"{stated[0]}% health, a {stated[1]}% chance and two guards.")


def test_royal_guard_row_still_says_a_chance_of_guards_of_a_higher_rank():
    """The three phrases the rule's readings rest on.

    "CHANCE" is why a beat rolls rather than calling guards every time. "SUMMON TWO
    GUARDS" is why creatures are spawned at all, and how many. "OF THE NEXT HIGHER RANK"
    is why their rarity rung is written after they spawn, which is what makes them
    stronger than the creature that called them.
    """
    words = flat(rows()["War_Royal_Guard"]["Description"]).lower()

    assert "chance" in words, (
        "The Royal Guard row no longer says there is a chance. The rule rolls against "
        "RoyalGuardChancePercent because it did. " + words)
    assert "summon two guards" in words, (
        "The Royal Guard row no longer says two guards are summoned. The rule spawns "
        "RoyalGuardGuardsSummoned creatures because it did. " + words)
    assert "next higher rank" in words, (
        "The Royal Guard row no longer says the guards are of the next higher rank. The "
        "rule writes their rarity rung after spawning them because it did. " + words)


def test_royal_guard_row_still_says_uncommon_so_the_ruling_is_read_again():
    """The row names a rank no ladder in this game has, and the rule had to be ruled.

    "ABOVE UNCOMMON RANKED". game/Data/EnemyRarities.csv is Common, Elite, Legendary,
    Herald, Boss, Cataclysm Boss; the only Uncommon in the game's data is the second
    crafting material tier, and gear uses a third vocabulary again. It was ruled to mean
    Elite and above.

    THIS CHECK FAILS WHEN THE ROW IS REWORDED, WHICH IS THE POINT, and the reword is
    already decided: the project owner chose "above Common ranked" on 2026-09-17, and the
    row's text lives in the design workbook, so it lands in a later change than the rule.
    WHEN THIS FAILS WITH THAT WORDING, replace the word this pins and leave
    RoyalGuardLowestRungThatSummons alone: "above Common ranked" is Elite and above, and
    docs/DECISIONS.md records the decision. Any OTHER new wording has to be read again
    from scratch.
    """
    words = flat(rows()["War_Royal_Guard"]["Description"]).lower()
    ladder = flat((REPO_ROOT / "game" / "Data" / "EnemyRarities.csv")
                  .read_text(encoding="utf-8")).lower()

    assert "uncommon" in words, (
        "The Royal Guard row no longer says \"above Uncommon ranked\". If it now says "
        "\"above Common ranked\", that is the reword the project owner chose on "
        "2026-09-17: pin that wording here instead and leave "
        "RoyalGuardLowestRungThatSummons at Elite. Any other wording has to be read again "
        "and the rung decided against it. " + words)
    assert "uncommon" not in ladder, (
        "game/Data/EnemyRarities.csv now has an Uncommon rung. The ruling that the row's "
        "word names nothing was made when it had none; re-read it.")


def test_royal_guards_ceiling_is_the_mutation_rules_ceiling_and_not_its_own_number():
    """Both rules stop below the first boss rung, and they say so in one place.

    A SECOND 3 HERE WOULD BE THE SAME FACT TWICE with nothing holding the two together,
    and only one of them is tied to ACataclysmEnemyCharacter::FirstBossRarityStep by the
    static_assert the check above this one guards.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    assert re.search(
        r"\bRoyalGuardHighestRung\s*=\s*VolatileEvolutionHighestRung\s*;", text), (
        "RoyalGuardHighestRung is no longer declared as VolatileEvolutionHighestRung. If "
        "the two rules are meant to stop at different rungs now, say why in "
        "docs/DECISIONS.md and tie the new one to the first boss rung as the other is.")


def test_demon_prince_row_states_no_number_of_its_own():
    """Every figure this rule uses is judged: its row states none.

    "OCCASSIONALLY" IS THE ROW'S WHOLE STATEMENT OF THE CHANCE. If the row ever states a
    figure -- a percentage, a count, a rank -- this fails, so the constant is read off the
    row and docs/DECISIONS.md stops calling it a judgement.
    """
    words = flat(rows()["Demonic_Demon_Prince"]["Description"])

    assert "%" not in words, words
    assert not [c for c in words if c.isdigit()], (
        "The Demon Prince row now states a number. Check DemonPrinceChancePercent, "
        "DemonPrincesPerFloor and DemonPrinceRung against it and update "
        "docs/DECISIONS.md. " + words)


def test_demon_prince_row_still_says_a_kill_you_made_brings_one_from_the_corpse():
    """The three phrases the rule's readings rest on, in the row's own spelling.

    "OCCASSIONALLY" is why a death rolls rather than always bringing one. "WHEN YOU SLAY AN
    ENEMY" is why this is the first of these death rules to ask who did the killing. "RIP
    OUT THROUGH IT'S CORPSE" is why what rises stands where the creature died and is of its
    kind.

    THE ROW'S OWN SPELLINGS ARE PINNED AS THEY STAND -- "Occassionally" and "it's" -- so a
    silent tidy fails this check and is put to the project owner as a reword, the way the
    Royal Guard row's "above Uncommon ranked" was. The design data is the design; nobody
    corrects it on the way past.
    """
    words = flat(rows()["Demonic_Demon_Prince"]["Description"])

    assert "Occassionally" in words, (
        "The Demon Prince row no longer opens with its own spelling of \"Occassionally\". "
        "If that was a deliberate reword, read the sentence again and update this check "
        "and docs/DECISIONS.md; if it was a tidy, the design data is the design. " + words)
    assert "when you slay an enemy" in words.lower(), (
        "The Demon Prince row no longer says the player must do the killing. The rule asks "
        "for the killer to be the player and the blow not to be dealt by a minion because "
        "it did. " + words)
    assert "it's corpse" in words.lower(), (
        "The Demon Prince row no longer says the creature rises from the slain one's "
        "corpse -- in the row's own spelling, \"it's\". What rises stands on the cell the "
        "creature died on because it did. " + words)


def test_demon_princes_chance_is_its_own_number_and_not_another_rules():
    """The chance is a judgement of its own, deliberately not bound to a neighbour.

    TEN IS THE HOUSE FIGURE for a chance on a death or a hit, and this row starts from it.
    Binding it to another rule's constant would say the two must move together, which the
    ruling did not say. Royal Guard's fifty is not a precedent either: that row states its
    own figure and this one does not.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    assert re.search(r"\bDemonPrinceChancePercent\s*=\s*\d+(?:\.\d+)?f\s*;", text), (
        "DemonPrinceChancePercent is no longer a number of its own. If it is now another "
        "rule's figure, say why in docs/DECISIONS.md.")
    assert re.search(r"\bDemonPrincesPerFloor\s*=\s*\d+\s*;", text), (
        "DemonPrincesPerFloor is no longer a number of its own.")


def test_demon_princes_rung_is_the_shared_ceiling_and_not_a_third_number():
    """Three rules now stop below the first boss rung, and they say so in one place.

    A THIRD 3 WOULD BE THE SAME FACT A THIRD TIME with nothing holding the copies together.
    Only Volatile Evolution's constant is tied to
    ACataclysmEnemyCharacter::FirstBossRarityStep, by the static_assert a check above this
    one guards, and the other two are declared as that constant.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    assert re.search(
        r"\bDemonPrinceRung\s*=\s*VolatileEvolutionHighestRung\s*;", text), (
        "DemonPrinceRung is no longer declared as VolatileEvolutionHighestRung. If this "
        "rule is meant to stop at a different rung now, say why in docs/DECISIONS.md and "
        "tie the new figure to the first boss rung as that one is.")


def test_epidemic_row_still_states_its_two_figures():
    """Two of this rule's figures are the row's own, not judgements.

    "A 25% CHANCE" and "SPREADS 5 TIMES IN A SINGLE CHAIN". If either moves in the row and
    not in the header, the rule stops being the row, and docs/DECISIONS.md stops being able
    to call only the others judgements.
    """
    words = flat(rows()["Pestilence_Epidemic"]["Description"])
    percent = re.findall(r"(\d+(?:\.\d+)?)\s*%", words)
    counts = re.findall(r"spreads (\d+) times", words)

    assert percent == ["25"], (
        "The Pestilence Epidemic row no longer states a 25% chance. Check "
        "EpidemicSpreadChancePercent against it and update docs/DECISIONS.md. " + words)
    assert counts == ["5"], (
        "The Pestilence Epidemic row no longer says the disease spreads 5 times in a "
        "chain. Check EpidemicSpreadsToKill against it. " + words)

    text = EFFECTS_HEADER.read_text(encoding="utf-8")
    missing = [name for name, pattern in (
        ("EpidemicSpreadChancePercent", r"EpidemicSpreadChancePercent\s*=\s*25\.0f\s*;"),
        ("EpidemicSpreadsToKill", r"EpidemicSpreadsToKill\s*=\s*5\s*;"),
    ) if not re.search(pattern, text)]

    assert not missing, (
        f"{', '.join(missing)} no longer holds the figure the row states: a "
        f"{percent[0]}% chance and a chain of {counts[0]}.")


def test_epidemic_row_still_says_a_kill_you_made_spreads_a_disease():
    """The four phrases the rule's readings rest on.

    "WHEN YOU KILL" is why this asks who did the killing. "A DISEASED ENEMY" is why a
    creature carrying nothing that could pass on is outside the rule and leaves the chain
    alone. "ALL OF THE DEAD ENEMY'S REMAINING DEBUFFS" is why the rule asks the contagion
    library for the whole list rather than for one. "PLAGUE LORD" is why a creature is
    spawned at the end of a chain at all.
    """
    words = flat(rows()["Pestilence_Epidemic"]["Description"]).lower()

    assert "when you kill" in words, (
        "The Pestilence Epidemic row no longer says the player does the killing. The rule "
        "asks whether the killer is the player because it did. " + words)
    assert "diseased enemy" in words, (
        "The Pestilence Epidemic row no longer says the enemy must be diseased. The rule "
        "asks the corpse for a debuff that could pass on because it did. " + words)
    assert "all of the dead enemy's remaining debuffs" in words, (
        "The Pestilence Epidemic row no longer says ALL of the dead enemy's debuffs pass "
        "on. The rule asks UCataclysmContagion::EverySpreadable for the whole list rather "
        "than PickSpreadable for one because it did. " + words)
    assert "plague lord" in words, (
        "The Pestilence Epidemic row no longer ends a chain with a Plague Lord. " + words)


def test_epidemics_reach_is_the_contagion_librarys_and_not_a_number_of_its_own():
    """The reach is the one the spreading nodes already have, written as that constant.

    THE ROW STATES NO DISTANCE -- "a nearby enemy", "all nearby enemies" -- so the figure
    is judged, and the judgement was to take the reach the contagion library already uses
    rather than to choose a second six. A number here would be the same design figure
    written twice with nothing holding the two together.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    assert re.search(
        r"\bEpidemicRadiusMetres\s*=\s*UCataclysmContagion::RadiusMetres\s*;", text), (
        "EpidemicRadiusMetres is no longer declared as UCataclysmContagion::RadiusMetres. "
        "If this rule is meant to reach further than the spreading nodes now, say why in "
        "docs/DECISIONS.md.")

    words = flat(rows()["Pestilence_Epidemic"]["Description"]).lower()
    assert "nearby" in words, (
        "The Pestilence Epidemic row no longer says the disease reaches a NEARBY enemy. "
        "If it now states a distance, read it off the row instead of the library. " + words)


def test_epidemics_plague_lord_rung_is_the_shared_ceiling():
    """A fourth rule now stops below the first boss rung, and they say so in one place.

    Only Volatile Evolution's constant is tied to
    ACataclysmEnemyCharacter::FirstBossRarityStep, by the static_assert an earlier check
    guards; Royal Guard's, Demon Prince's and this one are declared as that constant.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    assert re.search(
        r"\bEpidemicPlagueLordRung\s*=\s*VolatileEvolutionHighestRung\s*;", text), (
        "EpidemicPlagueLordRung is no longer declared as VolatileEvolutionHighestRung. If "
        "a Plague Lord is meant to stand at a different rung now, say why in "
        "docs/DECISIONS.md and tie the new figure to the first boss rung as that one is.")


def test_blood_forged_champions_row_still_says_elites_absorb_nearby_dying_allies():
    """The three phrases this rule's readings rest on.

    "ELITE ENEMIES" is why the rule asks the rank. "NEARBY DYING ALLIES" is why it asks
    the distance AND why it asks no killer: the row names the survivor and nobody else.
    "MINI-BOSS" is why the rule stops at Herald rather than anywhere else.
    """
    words = flat(rows()["War_Blood_Forged_Champions"]["Description"]).lower()

    assert "elite enemies" in words, (
        "The War Blood-Forged Champions row no longer says ELITE enemies absorb. The rule "
        "refuses a Common because it did. " + words)
    assert "nearby dying allies" in words, (
        "The War Blood-Forged Champions row no longer says NEARBY DYING ALLIES. The rule "
        "measures a distance, and asks no killer at all, because it did. " + words)
    assert "mini-boss" in words, (
        "The War Blood-Forged Champions row no longer ends at a mini-boss. The rule stops "
        "at Herald because CataclysmEnemyCharacter.h calls Herald a mini-boss. " + words)


def test_blood_forged_champions_row_states_no_figure_of_its_own():
    """EVERY ONE OF THIS RULE'S FOUR FIGURES IS A JUDGEMENT, and this is what says so.

    The row carries no number at all: not a distance, not a count of deaths, not a rank
    expressed as a number. That is why docs/DECISIONS.md marks all four as judgements
    ruled under the project owner's delegation. If a number ever appears in the row, one
    of those judgements has been overtaken by the design and has to be re-read off it.
    """
    words = flat(rows()["War_Blood_Forged_Champions"]["Description"])
    digits = re.findall(r"\d+(?:\.\d+)?", words)

    assert digits == [], (
        "The War Blood-Forged Champions row now states a number, and this rule's figures "
        "were all chosen as judgements on the understanding that it stated none. Read the "
        f"new figure off the row and correct docs/DECISIONS.md. Found {digits} in: {words}")


def test_blood_forged_champions_reach_is_the_contagion_librarys_and_not_a_number():
    """"Nearby" means one distance in a floor rule, not two.

    THE ROW STATES NO DISTANCE, so the figure is judged, and the judgement was to take
    the reach Epidemic already uses for the same word rather than to choose a second six.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    assert re.search(
        r"\bBloodForgedChampionsRadiusMetres\s*=\s*UCataclysmContagion::RadiusMetres\s*;",
        text), (
        "BloodForgedChampionsRadiusMetres is no longer declared as "
        "UCataclysmContagion::RadiusMetres. If a champion is meant to reach further than "
        "a disease does, say why in docs/DECISIONS.md.")


def test_blood_forged_champions_rungs_are_the_two_constants_that_already_mean_them():
    """Neither end of this rule's ladder is a number of its own.

    The bottom is RoyalGuardLowestRungThatSummons, which already carries this project's
    answer to which creatures count as Elite, with the reading of EnemyRarities.csv that
    produced it. The top is VolatileEvolutionHighestRung, which is tied to
    ACataclysmEnemyCharacter::FirstBossRarityStep by the static_assert an earlier check
    guards, so no floor rule can make a boss out of an ordinary creature.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    missing = [name for name, pattern in (
        ("BloodForgedChampionsLowestRung",
         r"\bBloodForgedChampionsLowestRung\s*=\s*RoyalGuardLowestRungThatSummons\s*;"),
        ("BloodForgedChampionsHighestRung",
         r"\bBloodForgedChampionsHighestRung\s*=\s*VolatileEvolutionHighestRung\s*;"),
    ) if not re.search(pattern, text)]

    assert not missing, (
        f"{', '.join(missing)} is no longer declared as the constant that already means "
        "that rung. Two rules meaning the same rung must not be able to drift apart; if "
        "this rule is meant to start or stop somewhere else now, say why in "
        "docs/DECISIONS.md.")


def test_vengeful_wraiths_row_still_states_its_three_figures():
    """Three of this rule's four figures are the row's own, not judgements.

    "A 10% CHANCE", "90% DAMAGE REDUCTION" and "20% INCREASED DAMAGE, MOVESPEED, AND
    ATTACK SPEED". If any of them moves in the row and not in the header, the rule stops
    being the row, and docs/DECISIONS.md stops being able to call only the fourth a
    judgement.
    """
    words = flat(rows()["Death_Vengful_Wraiths"]["Description"])
    percents = re.findall(r"(\d+(?:\.\d+)?)\s*%", words)

    assert percents == ["10", "90", "20"], (
        "The Death Vengeful Wraiths row no longer states a 10% chance, 90% damage "
        "reduction and a 20% increase, in that order. Read the new figures off the row "
        f"and correct docs/DECISIONS.md. Found {percents} in: {words}")

    text = EFFECTS_HEADER.read_text(encoding="utf-8")
    missing = [name for name, pattern in (
        # THE CHANCE IS HELD IN TWO PLACES ON PURPOSE. Spore Clouds' constant was derived
        # from this row weeks before the row had a rule, so the row's ten lives there and
        # this rule's constant names it rather than writing ten again. The check below
        # reads both: the figure against the row, and the tie between the two rules.
        ("SporeCloudsChancePercentOnDeath",
         r"SporeCloudsChancePercentOnDeath\s*=\s*10\.0f\s*;"),
        ("VengefulWraithsChancePercent",
         r"VengefulWraithsChancePercent\s*=\s*SporeCloudsChancePercentOnDeath\s*;"),
        ("VengefulWraithsDamageReductionMore",
         r"VengefulWraithsDamageReductionMore\s*=\s*90\.0f\s*;"),
        ("VengefulWraithsIncreasePercent",
         r"VengefulWraithsIncreasePercent\s*=\s*20\.0f\s*;"),
    ) if not re.search(pattern, text)]

    assert not missing, (
        f"{', '.join(missing)} no longer holds the figure the row states: a "
        f"{percents[0]}% chance, {percents[1]}% damage reduction and a {percents[2]}% "
        "increase.")


def test_vengeful_wraiths_row_still_says_who_killed_them_and_how_far():
    """The two phrases the rule's readings rest on.

    "THE ONE WHO KILLED THEM" is why the rule asks who did the killing, and therefore why
    a kill by another creature, or by a minion whose summoner has not bought the Conduit
    keystone, raises nothing. "ACROSS THE ENTIRE DUNGEON" is why a wraith is spawned with
    a sight multiplier at all.
    """
    words = flat(rows()["Death_Vengful_Wraiths"]["Description"]).lower()

    assert "the one who killed them" in words, (
        "The Death Vengeful Wraiths row no longer says the wraith is roused by THE ONE "
        "WHO KILLED THEM. The rule asks whether the killer was the player because it "
        "did. " + words)
    assert "across the entire dungeon" in words, (
        "The Death Vengeful Wraiths row no longer says a wraith hunts ACROSS THE ENTIRE "
        "DUNGEON. The sight multiplier exists because it did. " + words)


def test_vengeful_wraiths_ninety_is_above_the_additive_cap_and_goes_in_the_other_layer():
    """WHY THE ROW'S 90 IS NOT WRITTEN WHERE IT LOOKS LIKE IT SHOULD BE.

    UCataclysmDamageCalculation::DamageReductionCap bounds the ADDITIVE pool, and the row
    asks for more than it allows, so 90 written there would read as the cap and the row's
    number would not be what happens in play. The project owner decided on 2026-09-17 that
    it goes into the multiplicative bucket instead, whose bound is MoreDamageReductionCap.

    THIS CHECK FAILS IF THE CAP EVER RISES TO MEET THE ROW, because the decision would
    then have been overtaken and the simpler layer would be available again.
    """
    calculation = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                   / "CataclysmDamageCalculation.h").read_text(encoding="utf-8")

    additive = re.search(r"DamageReductionCap\s*=\s*(\d+(?:\.\d+)?)f\s*;", calculation)
    multiplicative = re.search(
        r"MoreDamageReductionCap\s*=\s*(\d+(?:\.\d+)?)f\s*;", calculation)
    assert additive and multiplicative, (
        "CataclysmDamageCalculation.h no longer states both damage reduction bounds by "
        "the names this check reads. Find what replaced them before trusting this rule's "
        "figure.")

    figure = constant("VengefulWraithsDamageReductionMore")
    assert figure > float(additive.group(1)), (
        f"The additive damage reduction cap is now {additive.group(1)}, which is no "
        f"longer below this rule's {figure}. The owner's decision to write the row's "
        "figure into the multiplicative bucket was made because the additive pool could "
        "not carry it; that reason is gone, so revisit it in docs/DECISIONS.md.")
    assert figure <= float(multiplicative.group(1)), (
        f"This rule's {figure} is above the bound on one multiplicative source, "
        f"{multiplicative.group(1)}, so the calculation would clamp it and the row's "
        "number would not be what happens.")

    header = EFFECTS_HEADER.read_text(encoding="utf-8")
    assert "VengefulWraithsDamageReductionMore" in header, (
        "The constant is no longer named for the layer it is written into. The name is "
        "what tells a reader which of the two bounds applies.")


def test_vengeful_wraiths_sight_is_held_to_the_floors_own_span():
    """The sight figure is a judgement, and it is sized against the floor rather than picked.

    The row says "across the entire dungeon" and states no distance. The static_assert
    that holds the figure to the largest floor lives in CataclysmDungeonGameMode.cpp, not
    in the rule header, because that file already includes the floor generator and the Imp
    and the header includes neither. THIS CHECK IS WHAT NOTICES IF IT IS DELETED: a figure
    with nothing holding it is a figure that goes stale the next time the floor grows.
    """
    game_mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")

    for wanted in ("VengefulWraithsSightMultiplier",
                   "ImpNoticeRadiusCm",
                   "FCataclysmFloorGenerator::MostFloorSide",
                   "FCataclysmFloorGenerator::CellSizeCm"):
        assert wanted in game_mode, (
            f"CataclysmDungeonGameMode.cpp no longer names {wanted}. The compile-time "
            "check that a wraith can see across the largest floor this game builds reads "
            "all four; without it the sight figure is a number nothing holds.")

    assert re.search(r"static_assert\(\s*Effects::VengefulWraithsSightMultiplier",
                     game_mode), (
        "The static_assert sizing the wraith's sight against the floor is gone. Put it "
        "back or say in docs/DECISIONS.md what holds the figure instead.")


def test_judgment_zones_row_still_states_its_two_figures():
    """Two of this rule's figures are the row's own, not judgements.

    "SPAWN FOR 20 SECONDS" and "IF TRIGGERED 5+ TIMES". Everything else about this rule --
    how often ground appears, how many stand at once, how hard it ramps and how far, and
    what the loot bonus is worth -- was judged, and docs/DECISIONS.md can only call them
    judgements while these two are read off the row.
    """
    words = flat(rows()["Celestial_Judgment_Zones"]["Description"])
    seconds = re.findall(r"(\d+)\s*seconds", words)
    times = re.findall(r"(\d+)\+?\s*times", words)

    assert seconds == ["20"], (
        "The Celestial Judgment Zones row no longer says its zones stand for 20 seconds. "
        "Check JudgmentZonesSeconds against it. " + words)
    assert times == ["5"], (
        "The Celestial Judgment Zones row no longer says five triggers. Check "
        "JudgmentZonesTriggersForTheBonus against it. " + words)

    text = EFFECTS_HEADER.read_text(encoding="utf-8")
    missing = [name for name, pattern in (
        ("JudgmentZonesSeconds", r"JudgmentZonesSeconds\s*=\s*20\.0f\s*;"),
        ("JudgmentZonesTriggersForTheBonus",
         r"JudgmentZonesTriggersForTheBonus\s*=\s*5\s*;"),
    ) if not re.search(pattern, text)]

    assert not missing, (
        f"{', '.join(missing)} no longer holds the figure the row states: "
        f"{seconds[0]} seconds and {times[0]} triggers.")


def test_judgment_zones_damage_type_is_read_off_the_row_and_not_written():
    """"HOLY DAMAGE" NAMES NO DAMAGE TYPE THIS GAME HAS, and nothing rules on it.

    The eight types are the eight Cataclysms. The four built zone rules read the type off
    the row rather than writing one, and StepInfernalRain says why: "a row retyped in the
    workbook retypes its hazard with no code change. A constant here would be this file's
    opinion of the data." This row's CataclysmType is Celestial, so the mechanism answers
    the question -- and this check is what notices if somebody later writes the answer down.
    """
    words = flat(rows()["Celestial_Judgment_Zones"]["Description"]).lower()
    assert "holy damage" in words, (
        "The Celestial Judgment Zones row no longer says HOLY DAMAGE. If it now names a "
        "type this game has, read it off the row instead of the row's Cataclysm. " + words)

    assert rows()["Celestial_Judgment_Zones"]["CataclysmType"].strip() == "Celestial", (
        "The Celestial Judgment Zones row is no longer a Celestial row, so the damage its "
        "ground deals has changed type with it. That is the mechanism working; check "
        "docs/DECISIONS.md still describes what the rule does.")

    game_mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    step = body_of(game_mode, "void ACataclysmDungeonGameMode::StepJudgmentZones(")

    assert "Row->CataclysmType" in step, (
        "StepJudgmentZones no longer reads the damage type off the row. A type written "
        "here is this file's opinion of the data, and a row retyped in the workbook would "
        "stop retyping its hazard.")
    # THE LITERAL AND NOT THE WORD. The function's own comments explain that the row's
    # CataclysmType is Celestial, and a check on the bare word would fail on that prose --
    # it did, the first time this was run. What "writing the answer down" looks like in
    # code is a quoted literal, so that is what this refuses.
    assert 'TEXT("Celestial")' not in step, (
        "StepJudgmentZones now writes Celestial as a literal. The row's own CataclysmType "
        "is what types this rule's damage; writing it down here is what the four other "
        "zone rules deliberately do not do.")


def test_judgment_zones_reach_and_fall_are_the_shared_constants():
    """Neither distance in this rule is a number of its own.

    The row states no distance at all. The reach is the 300 cm that Infernal Rain,
    Singularity Wells and Withered Ground all use for a patch of ground, and how far a zone
    falls from the player is the 1200 Infernal Rain and Singularity Wells share.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    missing = [name for name, pattern in (
        ("JudgmentZonesRadiusCm",
         r"\bJudgmentZonesRadiusCm\s*=\s*WitheredGroundPatchRadiusCm\s*;"),
        ("JudgmentZonesFallsWithinCm",
         r"\bJudgmentZonesFallsWithinCm\s*=\s*InfernalRainFallsWithinCm\s*;"),
    ) if not re.search(pattern, text)]

    assert not missing, (
        f"{', '.join(missing)} is no longer declared as the constant that already means "
        "that distance. If radiant ground is meant to be a different size from every other "
        "patch this game lays, say why in docs/DECISIONS.md.")


def test_judgment_zones_three_at_once_follows_both_precedents():
    """Three is a vocabulary, so it is written as a figure and held to its precedents.

    Infernal Rain's InfernalRainMostPatches and Singularity Wells' SingularityWellsMostWells
    are both 3, written independently. That makes three what the table already says about
    how much ground may stand at once, rather than one design figure with two names, so
    this rule follows it WITHOUT being tied to either.

    NOT A COMPARISON OF A CONSTANT WITH ITSELF. All three are literals, so they can drift
    apart -- which is what this notices. The same treatment
    HolyRepercussionsChancePercentOnHit gets for the table's ten.
    """
    mine = whole_number("JudgmentZonesMostZones")
    rain = whole_number("InfernalRainMostPatches")
    wells = whole_number("SingularityWellsMostWells")

    assert mine == rain == wells, (
        f"How much ground may stand at once no longer agrees: Judgment Zones {mine}, "
        f"Infernal Rain {rain}, Singularity Wells {wells}. Three was followed as the "
        "figure this library already uses; if one of them is meant to differ now, say "
        "which and why in docs/DECISIONS.md.")


def test_march_of_progress_row_still_states_both_of_its_ten_per_cents():
    """Both of this rule's figures are the row's own, and it is the whole rule.

    "ENEMIES DAMAGE INCREASES BY 10%" and "INCREASE THE PLAYER'S ARMOR BY 10%". Nothing
    else in this rule is a number: how the damage accumulates is a reading of a sentence,
    and who the Commander is is a choice, but both quantities came off the row.

    TWO CONSTANTS HOLD ONE NUMBER, AND THAT IS DELIBERATE. They are opposite sides of the
    row's trade and tuning one must not move the other, so this check reads each against
    its own half of the sentence rather than checking that they agree with each other.
    """
    words = flat(rows()["War_March_of_Progress"]["Description"])
    lowered = words.lower()

    assert "enemies damage increases by 10%" in lowered, (
        "The War March of Progress row no longer says enemies' damage increases by 10% a "
        "floor. Check MarchOfProgressEnemyDamagePercentPerFloor against it. " + words)
    assert "armor by 10%" in lowered, (
        "The War March of Progress row no longer says the player's armor increases by "
        "10%. Check MarchOfProgressArmourPercentPerCommander against it. " + words)

    assert constant("MarchOfProgressEnemyDamagePercentPerFloor") == 10.0, (
        "MarchOfProgressEnemyDamagePercentPerFloor no longer holds the 10% the row "
        "states for enemies' damage.")
    assert constant("MarchOfProgressArmourPercentPerCommander") == 10.0, (
        "MarchOfProgressArmourPercentPerCommander no longer holds the 10% the row states "
        "for the player's armor.")


def test_march_of_progress_damage_is_additive_and_not_compounded():
    """"Each floor, enemies damage increases by 10%" is read as adding, not compounding.

    A JUDGEMENT ON A SENTENCE THAT COULD BE READ EITHER WAY, and the reading is what makes
    the rule survivable. Additive, the tenth floor is twice; compounded it is 2.59 times,
    and the fiftieth floor of a fifty-floor dungeon is 117 times, which no amount of
    armour answers. docs/DECISIONS.md records the rejected reading.

    THE ARITHMETIC IS CHECKED AND NOT THE COMMENT. A compounded rule would be written with
    a power, so this refuses one and requires the multiplication that makes it additive.
    """
    source = EFFECTS_SOURCE.read_text(encoding="utf-8")
    body = body_of(
        source,
        "float UCataclysmDungeonModifierEffects::MarchOfProgressDamageMultiplierOnFloor(")

    assert "MarchOfProgressEnemyDamagePercentPerFloor" in body, (
        "MarchOfProgressDamageMultiplierOnFloor no longer uses the row's own per-floor "
        "figure, so the 10% the row states reaches nothing.")
    assert not re.search(r"\bFMath::Pow\b|\bpowf?\s*\(", body), (
        "MarchOfProgressDamageMultiplierOnFloor now raises something to a power, which is "
        "the compounded reading this rule deliberately rejected. At fifty floors that is "
        "117 times the creature's damage. Say in docs/DECISIONS.md if the reading has "
        "changed.")
    assert re.search(r"1\.0f\s*\+", body), (
        "MarchOfProgressDamageMultiplierOnFloor no longer adds to 1, so it is no longer a "
        "multiplier on the creature's designed damage.")


def test_march_of_progress_uses_the_creatures_named_multiplier_and_not_its_stat_inputs():
    """The floor's damage rise goes through the creature's own third named multiplier.

    THE MECHANISM EXISTED BEFORE THIS RULE AND THIS RULE WAS FIRST WRITTEN WITHOUT IT.
    `ACataclysmEnemyCharacter` already carried two named damage multipliers, one for the
    wave that placed a creature (Death_Grave_Tide) and one for how long it has lived
    (Famine_Ravenous_Hoard), with its header saying why there are two and not one: "two
    rules can act on one creature and neither may overwrite the other". This rule adds a
    third rather than writing the creature's stat inputs, which are replaced wholesale and
    would have been exactly that hazard.

    THE CHECK IS ON THE WRITE AND NOT ON THE COMMENT, so deleting the call fails it.
    """
    game_mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    step = body_of(game_mode, "void ACataclysmDungeonGameMode::StepMarchOfProgress(")

    assert "SetFloorDepthDamageMultiplier" in step, (
        "StepMarchOfProgress no longer sets the creature's floor-depth damage multiplier, "
        "so the floor's damage increase reaches no creature.")
    assert "SetStatInputs" not in step, (
        "StepMarchOfProgress now writes a creature's stat inputs. SetStatInputs replaces "
        "the whole recorded set, so a later rule writing one stat would silently drop "
        "this one. The creature's named multipliers exist so that cannot happen.")

    creature = (
        REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
        / "CataclysmEnemyCharacter.cpp").read_text(encoding="utf-8")
    write = body_of(creature, "void ACataclysmEnemyCharacter::WriteAttackDamage(")

    # ONE MAP SINCE GOLDEN SPIRES, as ruled on 2026-09-17: the three named fields this
    # checked became entries of `DamageMultipliersBySource`, and WriteAttackDamage
    # multiplies by the product of every entry.
    assert "DamageMultiplierProduct()" in write, (
        "WriteAttackDamage no longer multiplies by DamageMultiplierProduct(). Every floor "
        "rule that changes a creature's damage owns an entry of DamageMultipliersBySource, "
        "and a rule whose multiplier is dropped here does nothing at all, silently.")
    product = body_of(creature, "float ACataclysmEnemyCharacter::DamageMultiplierProduct(")
    assert "DamageMultipliersBySource" in product and "*=" in product, (
        "DamageMultiplierProduct no longer multiplies the entries of "
        "DamageMultipliersBySource together.")

    # AND EACH SOURCE WRITES ITS OWN KEY, so no two share an entry.
    header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
              / "CataclysmEnemyCharacter.h").read_text(encoding="utf-8")
    keys = {}
    for setter, key in (("SetPlacedDamageMultiplier", "PlacedDamageSource"),
                        ("SetTimeAliveDamageMultiplier", "TimeAliveDamageSource"),
                        ("SetFloorDepthDamageMultiplier", "FloorDepthDamageSource"),
                        ("SetSpireDamageMultiplier", "SpireDamageSource"),
                        ("SetPlagueBeaconsDamageMultiplier", "PlagueBeaconsDamageSource"),
                        ("SetTrialOfEnduranceDamageMultiplier", "TrialOfEnduranceDamageSource"),
                        ("SetObsidianSarcophagiDamageMultiplier", "ObsidianSarcophagiDamageSource")):
        body = body_of(creature, f"void ACataclysmEnemyCharacter::{setter}(")
        assert f"SetDamageMultiplierFrom({key}," in body, (
            f"{setter} no longer writes its own key, {key}, so its rule's multiplier "
            "shares an entry with another's or reaches none.")
        found = re.search(rf'{key}\s*=\s*TEXT\("(\w+)"\)', header)
        assert found, f"{key} is no longer declared in CataclysmEnemyCharacter.h."
        keys[key] = found.group(1)
    assert len(set(keys.values())) == len(keys), (
        f"Two damage sources share a key, so one overwrites the other: {keys}")


def test_march_of_progress_armour_is_one_modifier_and_not_one_per_commander():
    """Three commanders are 1.3 times the armour, not 1.331 times.

    EVERY DUNGEON RULE'S STAT MODIFIER GOES IN THE "MORE" BUCKET, and
    `UCataclysmStatPipeline` multiplies each source on its own rather than summing them
    first. So the whole figure has to be carried by ONE modifier: three separate 10%
    modifiers would compound, and "10% per commander" would stop meaning what it says.

    THE ARITHMETIC MULTIPLIES THE COUNT, which is what makes one modifier enough.
    """
    source = EFFECTS_SOURCE.read_text(encoding="utf-8")
    body = body_of(
        source,
        "float UCataclysmDungeonModifierEffects::MarchOfProgressArmourMorePercentFor(")

    assert "MarchOfProgressArmourPercentPerCommander" in body, (
        "MarchOfProgressArmourMorePercentFor no longer uses the row's own per-commander "
        "figure, so the 10% the row states reaches nothing.")
    assert "CommandersKilled" in body, (
        "MarchOfProgressArmourMorePercentFor no longer reads how many commanders were "
        "killed, so every count pays the same.")

    modifiers = body_of(
        source,
        "TMap<FName, TArray<FCataclysmStatModifier>> "
        "UCataclysmDungeonModifierEffects::StatModifiersFor(")
    writes = modifiers.count("Effects.ArmourMorePercent")

    assert writes == 1, (
        f"StatModifiersFor writes the March of Progress armour {writes} times rather than "
        "once. Each write is a separate More multiplier and the pipeline multiplies them "
        "separately, so more than one compounds: three 10% modifiers are 1.331 times "
        "rather than the 1.3 the row means.")

    assert 'DungeonModifierEffectsArmourStat = TEXT("armor")' in source, (
        "The armour stat is no longer spelled armor. UCataclysmPlayerClassStats::"
        "StatToAttribute holds that spelling, and a modifier keyed by a name it does not "
        "hold is written nowhere, silently.")


def test_march_of_progress_commander_is_chosen_and_marks_the_creature_with_nothing():
    """The floor's Commander is a creature this rule remembers, not one carrying a tag.

    THE COMMANDER GAMEPLAY TAG MEANS "BUFFED BY A COMMANDER" AND NOT "IS A COMMANDER".
    `ACataclysmEnemyCharacter::CommanderMultiplier` makes whoever carries it 20% faster,
    and both things that grant it give it to OTHER creatures. One tag with two meanings is
    what that class's own comment forbids, so this rule holds a weak pointer instead and
    writes nothing on the creature it chooses.

    WHAT A PLAYER WOULD NEED IN ORDER TO SEE WHICH CREATURE IT IS IS ISSUE #1997, filed
    rather than folded into this rule.
    """
    game_mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    chooser = body_of(game_mode, "void ACataclysmDungeonGameMode::ChooseTheFloorsCommander(")

    assert "MarchOfProgressCommander = " in chooser, (
        "ChooseTheFloorsCommander no longer records which creature it chose, so nothing "
        "can pay the player for killing it.")
    assert "RarityStep" in chooser, (
        "ChooseTheFloorsCommander no longer compares rungs, so the Commander is no longer "
        "the highest-rung creature the floor placed.")

    forbidden = [name for name in ("AddLooseGameplayTag", "CommanderTag", "SetRarityStep",
                                  "SetAttackDamage", "SetHealth")
                 if name in chooser]
    assert not forbidden, (
        f"ChooseTheFloorsCommander now changes the creature it chooses: {forbidden}. The "
        "ruling is that nothing on it changes -- the Commander tag already means 'buffed "
        "by a commander', and marking the creature in play is issue #1997.")


def test_march_of_progress_puts_every_creatures_damage_back_on_a_floor_change():
    """A creature that lives through a change of floor stops carrying the last one's rise.

    A HORDE DUNGEON'S WAVES SHARE ONE ARENA, so a creature can live through the floor
    change, and the next floor may not carry this row at all. The two multipliers already
    on the creature are put back to 1.0 in one loop over every creature in the world, for
    exactly this reason; this rule's has to go in the same loop.

    THE COUNT OF COMMANDERS KILLED MUST NOT BE IN THAT RESET. The row pays it "in each
    level" and never takes it back, so it ends with the run and not with the floor.
    """
    game_mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    floor_change = body_of(game_mode,
                           "void ACataclysmDungeonGameMode::ApplyFloorRulesToPlayer(")

    assert "SetFloorDepthDamageMultiplier(1.0f)" in floor_change, (
        "A floor change no longer puts every creature's floor-depth damage multiplier "
        "back. A creature that lives through a Horde dungeon's change of wave would keep "
        "the last floor's damage rise on a floor that may not carry the row.")
    assert "MarchOfProgressArmourApplied = 0.0f" in floor_change, (
        "A floor change no longer forgets how much March of Progress armour was applied. "
        "The apply replaces the floor's modifiers wholesale, so the armour comes off the "
        "character and the next beat would believe it was still on.")
    assert "MarchOfProgressCommandersKilled = 0" not in floor_change, (
        "A floor change now clears how many commanders the player has killed. The row "
        "pays for the Commander 'in each level' and never takes it back, so that count "
        "ends with the run -- in LeaveEmpireDungeon -- and not at the stairs.")

    leaving = body_of(game_mode, "void ACataclysmDungeonGameMode::LeaveEmpireDungeon(")
    assert "MarchOfProgressCommandersKilled = 0" in leaving, (
        "Leaving the dungeon no longer clears how many commanders were killed, so the "
        "armour earned in one run would still be on the player in the next.")


def test_march_of_progress_forgets_its_commander_before_the_floor_is_populated():
    """The floor's Commander is forgotten where the floor's creatures are decided.

    THIS IS AN ORDERING TRAP AND IT WAS HIT WHILE BUILDING THE RULE. Every other per-floor
    field this rule holds is cleared in ApplyFloorRulesToPlayer with the rest of the
    dungeon rules' per-floor state. The Commander cannot be, because GoToFloor calls
    PopulateFloor -- which chooses the Commander -- and calls ApplyFloorRulesToPlayer
    AFTERWARDS. Clearing it there wipes the Commander that was just chosen, so every floor
    has none, the player can never be paid, and nothing fails: the damage half of the rule
    still works and the panel still prints a line.

    SO THE CHECK IS ON BOTH SIDES. It has to be forgotten in PopulateFloor and it must not
    be forgotten in the applier.
    """
    game_mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    populate = body_of(game_mode, "int32 ACataclysmDungeonGameMode::PopulateFloor(")
    applier = body_of(game_mode,
                      "void ACataclysmDungeonGameMode::ApplyFloorRulesToPlayer(")

    assert "MarchOfProgressCommander = nullptr" in populate, (
        "PopulateFloor no longer forgets the last floor's Commander, so a floor whose "
        "Commander is still alive keeps it and the new floor's creatures are never "
        "considered.")
    assert "bMarchOfProgressCommanderSlain = false" in populate, (
        "PopulateFloor no longer forgets that the last floor's Commander was killed, so "
        "the chooser returns early for ever and no later floor has a Commander at all.")

    assert "MarchOfProgressCommander = nullptr" not in applier, (
        "ApplyFloorRulesToPlayer now clears the floor's Commander. GoToFloor populates "
        "the floor first and applies the floor rules afterwards, so this wipes the "
        "Commander the population pass just chose: every floor would have none and no "
        "test of the damage half would notice.")
    assert "bMarchOfProgressCommanderSlain = false" not in applier, (
        "ApplyFloorRulesToPlayer now clears whether the Commander was slain, which runs "
        "after the population pass chose one. Forget it in PopulateFloor instead.")

    # THE ORDER ITSELF, SO THE REASON ABOVE CANNOT QUIETLY STOP BEING TRUE. If GoToFloor
    # is ever rearranged to apply the floor rules before populating, the two assertions
    # above become the wrong way round and this is what says so.
    go = body_of(game_mode, "bool ACataclysmDungeonGameMode::GoToFloor(")
    assert go.index("PopulateFloor()") < go.index("ApplyFloorRulesToPlayer()"), (
        "GoToFloor now applies the floor rules before populating the floor. March of "
        "Progress forgets its Commander in PopulateFloor precisely because that ran "
        "first; with the order swapped, the Commander must be forgotten in the applier "
        "instead. Check docs/DECISIONS.md and move it.")


def test_commanders_aura_row_still_says_elite_and_still_only_suggests_its_buffs():
    """This rule reads one word of its row as a rank and declines the row's examples.

    "CERTAIN ELITE ENEMIES" IS THE WHOLE OF WHAT THE ROW SAYS ABOUT WHO COMMANDS. It
    states no number, so "certain" is read as the rank and not as a count, and every
    creature at Elite or above commands.

    AND THE ROW ONLY SUGGESTS ITS BUFFS. It says "e.g., increased health, damage, or
    resistance"; the rule grants the Commander tag, which raises movement speed and
    attack speed. That is defensible only while the row's wording stays an example rather
    than a requirement, so this check reads the "e.g." as well as the word "elite". If
    the row is ever reworded to require those stats, this fails and docs/DECISIONS.md has
    to be revisited -- in particular the measured reason maximum health was excluded from
    that tag on 2026-08-20.
    """
    words = flat(rows()["War_Commander_s_Aura"]["Description"])
    lowered = words.lower()

    assert "elite" in lowered, (
        "The War Commander's Aura row no longer says which enemies command. Check "
        "CommandersAuraLowestRung against whatever it says now. " + words)
    assert "e.g." in lowered, (
        "The War Commander's Aura row no longer offers its buffs as examples. The rule "
        "grants movement and attack speed instead of the health, damage and resistance "
        "the row names, and that reading depended on the 'e.g.'. " + words)
    assert "nearby allies" in lowered, (
        "The War Commander's Aura row no longer says the buff goes to nearby allies, "
        "which is what CommandersAuraRadiusCm measures. " + words)


def test_commanders_aura_borrows_both_of_its_figures_rather_than_inventing_them():
    """Neither the reach nor the buff's length is a number of this rule's own.

    THE REACH IS THE SUCCUBUS'S. `ACataclysmSuccubusCharacter` grants this same buff to
    every ally within 800 cm, so a second distance for the same buff would mean the game
    empowered creatures at two ranges with no row asking for it. The figure is written in
    the rule library and checked against the creature here, because that library does not
    include the creature classes.

    THE LENGTH IS HALLOWED GROUNDFALL'S, and it is DECLARED as that constant rather than
    copied, so this check reads the declaration rather than the number.
    """
    succubus = "game/Source/Cataclysm/Character/CataclysmSuccubusCharacter.h"
    mine = constant("CommandersAuraRadiusCm")
    theirs = constant("DominionRadiusCm", succubus)

    assert mine == theirs, (
        f"Commander's Aura reaches {mine} cm and the Succubus's Dominion reaches "
        f"{theirs} cm. Both grant the same buff to a creature's nearby allies, so the "
        "game would empower at two ranges with no row asking it to. If one is meant to "
        "differ now, say which and why in docs/DECISIONS.md.")

    text = EFFECTS_HEADER.read_text(encoding="utf-8")
    assert re.search(
        r"CommandersAuraGrantSeconds\s*=\s*HallowedGroundfallEmpowerSeconds\s*;", text), (
        "CommandersAuraGrantSeconds no longer names HallowedGroundfallEmpowerSeconds. "
        "Both grant this buff from the same quarter-second beat in the same shape; a "
        "copied number can drift from the one it was copied from.")
    assert re.search(
        r"CommandersAuraLowestRung\s*=\s*RoyalGuardLowestRungThatSummons\s*;", text), (
        "CommandersAuraLowestRung no longer names RoyalGuardLowestRungThatSummons. That "
        "constant carries this project's answer to which creatures count as Elite, with "
        "the reading of game/Data/EnemyRarities.csv that produced it.")


def test_commanders_aura_passes_the_commander_as_the_instigator_and_the_ally_as_target():
    """The grant's arguments are (Instigator, Target), and this was written backwards.

    THE TWO NEIGHBOURING GRANTS OF THIS BUFF CANNOT TELL THE ORDER APART, because both
    pass the same actor twice: Hallowed Groundfall grants to a creature standing in its
    crater with that creature as instigator, and the enemy modifiers do the same.
    `ACataclysmSuccubusCharacter::PulseDominion` is the one that distinguishes them, and
    it passes `this, Ally`.

    WRITTEN THE OTHER WAY ROUND THE RULE BUFFS THE COMMANDER AND NOTHING ELSE, which
    compiles and reads correctly. It was written that way first. This check is what
    notices if it happens again.
    """
    game_mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    step = body_of(game_mode, "void ACataclysmDungeonGameMode::StepCommandersAura(")

    assert "ApplyTagForDuration" in step, (
        "StepCommandersAura no longer grants the buff, so no ally is ever empowered.")
    assert re.search(r"ApplyTagForDuration\(\s*\n?\s*Commander,\s*Ally,", step), (
        "StepCommandersAura no longer passes the commanding creature first and the ally "
        "second. The order is (Instigator, Target): reversed, the rule buffs the "
        "commander and nothing else, and it compiles.")
    assert "FindAlliesInSphere" in step, (
        "StepCommandersAura no longer asks for the commander's allies. That function is "
        "also what excludes the instigator, which is the whole of 'a commander does not "
        "buff itself'.")


def test_the_two_war_rules_name_their_own_rule_on_the_floor_panel():
    """One floor can carry both War rules and both use the word "Commander".

    THE WORD MEANS THREE THINGS: the gameplay tag means "buffed by a commander";
    `War_March_of_Progress` means the one creature a floor that the player hunts;
    `War_Commander_s_Aura` means every Elite buffing its neighbours. A player reading one
    panel needs to be able to tell the two lines apart, which is why the Aura's line names
    its rule and March of Progress's line names the creature.
    """
    game_mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    panel = body_of(game_mode,
                    "TMap<FName, FString> ACataclysmDungeonGameMode::LiveCountsForTheFloor(")

    assert "(Commander's Aura)" in panel, (
        "The floor panel line for Commander's Aura no longer names its own rule. A floor "
        "carrying March of Progress as well would show two lines using the word "
        "Commander for different things with nothing to tell them apart.")
    assert "this floor's Commander" in panel, (
        "The floor panel line for March of Progress no longer says which creature is "
        "that rule's Commander. Naming it is what keeps it apart from the Aura's "
        "commanders; see issue #1997 for what a player would still need beyond a name.")
    assert "ArchetypeNameForRow" in panel, (
        "The floor panel no longer reads the creature's name out of the archetype table. "
        "A name written in C++ stops matching the design workbook the first time a "
        "creature is renamed there.")



def test_anti_magic_zones_row_still_sets_magical_against_physical():
    """This rule reads "magical abilities" as the skills tagged `Type.Spell`.

    Ruled under the project owner's delegation: the row sets "magical abilities" against
    "physical skills", and `Type.Spell` is the one tag the skill data uses for that split.
    If the row is reworded so that it no longer draws that line, the reading has to be
    made again rather than quietly outlived.
    """
    row = rows()["Void_Anti_Magic_Zones"]
    words = flat(row["Description"]).lower()

    assert "magical abilities" in words and "physical skills" in words, (
        "The Void Anti-Magic Zones row no longer sets magical abilities against physical "
        "skills, which is the whole reason the rule locks only skills tagged Type.Spell. "
        "Read the new sentence and say in docs/DECISIONS.md what it now locks. " + words)
    assert row["CataclysmType"].strip() == "Void", (
        "The Anti-Magic Zones row is no longer a Void row. Its zones are drawn in the "
        "row's own type, so that is the mechanism working; check docs/DECISIONS.md still "
        "describes the rule.")
    assert not re.search(r"\d", words), (
        "The Anti-Magic Zones row now states a figure. Every figure of this rule is a "
        "shared constant copied because the row stated none; hold the rule to the row's "
        "own number instead. " + words)


def test_anti_magic_zones_figures_are_the_shared_constants():
    """None of this rule's figures is a number of its own.

    The row states no figure. Each is declared AS the constant it copies -- Judgment Zones'
    three zones, eight seconds and twenty seconds, the shared patch radius and fall
    distance, and the Edict of Silence's lock value -- so a reading of the declarations is
    the check. A static_assert beside them could never fail, because `X = Y` makes
    `X == Y` true by construction.
    """
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    missing = [name for name, copied in (
        ("AntiMagicZonesMostZones", "JudgmentZonesMostZones"),
        ("AntiMagicZonesSecondsBetweenZones", "JudgmentZonesSecondsBetweenZones"),
        ("AntiMagicZonesSeconds", "JudgmentZonesSeconds"),
        ("AntiMagicZonesRadiusCm", "WitheredGroundPatchRadiusCm"),
        ("AntiMagicZonesFallsWithinCm", "InfernalRainFallsWithinCm"),
        ("AntiMagicZonesLockValue", "EdictOfSilenceLockValue"),
    ) if not re.search(rf"\b{name}\s*=\s*{copied}\s*;", text)]

    assert not missing, (
        f"{', '.join(missing)} is no longer declared as the constant it copies. The row "
        "states no figure, so each was taken from the rule it resembles; if one is meant "
        "to differ now, say which and why in docs/DECISIONS.md.")


def without_comment_lines(text: str) -> str:
    """The text with every line that opens as a comment removed.

    A LINE OPENING WITH `*` IS TREATED AS A COMMENT, which is the convention every other
    check in this file uses; none of the functions read below wraps a product onto a line
    opening with `*`, and the checks name exact code lines rather than bare identifiers.
    """
    return "\n".join(line for line in text.splitlines()
                     if not line.lstrip().startswith(("//", "/*", "*")))


def test_anti_magic_zones_lock_is_scoped_to_the_spell_tag():
    """The lock reaches spells only, and an unresolved tag locks nothing rather than all.

    THREE THINGS, EACH OF WHICH ALONE WOULD LET THE RULE BECOME THE EDICT OF SILENCE.
    `StatModifiersFor` writes the rule's field through the TAG-SCOPED helper with the
    spell tag; that helper puts the tags on the modifier's `RequiredTags`; and it writes
    nothing for an empty scope, because a modifier with no required tags reaches every
    skill. The untagged helper beside it is checked unchanged, because every other rule
    calling it means "every skill".
    """
    code = without_comment_lines(EFFECTS_SOURCE.read_text(encoding="utf-8"))

    fold = body_of(code, "UCataclysmDungeonModifierEffects::StatModifiersFor(")
    assert re.search(
        r"DungeonModifierEffectsAddFlatForTags\(\s*Modifiers\s*,\s*"
        r"UCataclysmSkillSlots::LockedStat\s*,\s*Effects\.SpellsLockedValue\s*,\s*"
        r"Spells\s*\)", fold), (
        "StatModifiersFor no longer writes SpellsLockedValue onto skill_locked through the "
        "tag-scoped helper. Written through the untagged one, the anti-magic lock would "
        "refuse every skill, which is the Edict of Silence and not this row.")
    assert "UCataclysmSkillEffects::SpellTag()" in fold, (
        "StatModifiersFor no longer builds the anti-magic scope from "
        "UCataclysmSkillEffects::SpellTag(), the same tag IsSpell reads.")

    scoped = body_of(code, "void DungeonModifierEffectsAddFlatForTags(")
    assert "Modifier.RequiredTags = Tags;" in scoped, (
        "The tag-scoped helper no longer puts its tags on the modifier's RequiredTags, "
        "so what it writes reaches every skill.")
    assert re.search(r"if\s*\(\s*Value\s*<=\s*0\.0f\s*\|\|\s*Tags\.IsEmpty\(\)\s*\)",
                     scoped), (
        "The tag-scoped helper no longer refuses an empty scope. A tag the vocabulary has "
        "lost resolves to an invalid tag, the container stays empty, and the modifier "
        "would then lock every skill.")

    untagged = body_of(code, "void DungeonModifierEffectsAddFlat(")
    assert "RequiredTags" not in untagged, (
        "The untagged flat helper now sets RequiredTags. Every rule calling it means every "
        "skill; the scoped variant beside it exists so this one does not change.")


def test_a_dungeon_rule_modifiers_required_tags_reach_the_stat_line():
    """The fold: a dungeon rule's scoped modifier arrives at the cast with its scope.

    MEASURED BEFORE THE ANTI-MAGIC RULE WAS WRITTEN, AND HELD HERE. A scope is only as
    good as every hop that carries it, and a hop copying fields out of the modifier
    rather than the modifier whole would drop `RequiredTags` and turn a spell lock into
    a lock on everything, with no compile error and no C++ test on the hop itself:

      1. `UCataclysmDungeonModifierEffects::ApplyToCharacter` hands the whole map to the
         ability system (`SetDungeonStatModifiers`);
      2. `UCataclysmEquipmentComponent` appends each stat's modifiers WHOLE to the
         character's own map on every refresh;
      3. `UCataclysmPlayerClassStats` records the WHOLE list as the stat's inputs, which
         is what `StatForSkill` reads;
      4. `UCataclysmStatPipeline::ModifierApplies` judges every required tag with
         `HasTag`, so a parent tag reaches its children;
      5. `UCataclysmSkillTemplate` asks `StatForSkill` for `skill_locked` with the
         skill's OWN tags before it lets a press through.
    """
    source_root = REPO_ROOT / "game" / "Source" / "Cataclysm"

    def code(relative: str) -> str:
        return without_comment_lines((source_root / relative).read_text(encoding="utf-8"))

    apply = body_of(code("Dungeon/CataclysmDungeonModifierEffects.cpp"),
                    "bool UCataclysmDungeonModifierEffects::ApplyToCharacter(")
    assert "AbilitySystem->SetDungeonStatModifiers(StatModifiersFor(Effects));" in apply, (
        "Hop 1: ApplyToCharacter no longer hands the rule's modifiers to the ability "
        "system whole.")

    equipment = code("Items/CataclysmEquipmentComponent.cpp")
    assert re.search(
        r"Cataclysm->GetDungeonStatModifiers\(\)\)\s*\{\s*"
        r"Modifiers\.FindOrAdd\(Stat\.Key\)\.Append\(Stat\.Value\);", equipment), (
        "Hop 2: the equipment refresh no longer appends a dungeon rule's modifiers whole. "
        "Copying fields out of them would drop RequiredTags.")

    class_stats = code("Character/CataclysmPlayerClassStats.cpp")
    assert "ForStat = *Found;" in class_stats, (
        "Hop 3: the class stats no longer take a stat's whole modifier list.")
    assert "Recorded.Modifiers = ForStat;" in class_stats, (
        "Hop 3: the class stats no longer record that whole list as the inputs "
        "StatForSkill reads.")

    applies = body_of(code("AbilitySystem/CataclysmStatPipeline.cpp"),
                      "bool UCataclysmStatPipeline::ModifierApplies(")
    assert re.search(
        r"for\s*\(\s*const FGameplayTag& Required\s*:\s*Modifier\.RequiredTags\s*\)",
        applies), (
        "Hop 4: ModifierApplies no longer walks every required tag of the modifier.")
    assert "if (!SkillTags.HasTag(Required))" in applies, (
        "Hop 4: ModifierApplies no longer refuses a modifier whose required tag the skill "
        "does not carry, judged with HasTag.")

    template = code("AbilitySystem/CataclysmSkillTemplate.cpp")
    assert re.search(
        r"StatForSkill\(FName\(UCataclysmSkillSlots::LockedStat\),\s*SkillTags,\s*0\.0f\)",
        template), (
        "Hop 5: the skill template no longer asks for skill_locked with the skill's own "
        "tags, so a scoped lock could not tell one skill from another.")


def test_desperate_measures_row_still_states_its_two_figures():
    """Both of this rule's figures are the row's own, and neither is a judgement.

    "When your Mana falls below 10%, your skills cost 5% of your current Health to
    cast instead of Mana." `DesperateMeasuresManaBelowPercent` is the 10 and
    `DesperateMeasuresHealthPercent` the 5. If the row is reworded to another
    figure, the rule must follow it; if it stops saying "current", the health
    cost would be a share of the wrong pool.
    """
    words = flat(rows()["Famine_Desperate_Measures"]["Description"])
    percents = re.findall(r"(\d+)%", words)

    assert percents == ["10", "5"], (
        "The Famine Desperate Measures row no longer states 10% of mana and then 5% "
        "of health. Check DesperateMeasuresManaBelowPercent and "
        "DesperateMeasuresHealthPercent against it. " + words)
    assert "current Health" in words and "instead of Mana" in words, (
        "The row no longer says the health is CURRENT health and that it is paid "
        "INSTEAD of mana. Both readings are built into "
        "UCataclysmGameplayAbility::ManaCostPaidAsHealthPercent. " + words)
    assert re.search(r"\bbelow 10%", words), (
        "The row no longer says BELOW 10%. The condition mana_below is strictly "
        "below; an 'at or below' wording needs a different condition. " + words)

    assert constant("DesperateMeasuresManaBelowPercent") == 10.0, (
        "DesperateMeasuresManaBelowPercent no longer holds the row's 10.")
    assert constant("DesperateMeasuresHealthPercent") == 5.0, (
        "DesperateMeasuresHealthPercent no longer holds the row's 5.")


def test_divine_resurgence_row_still_states_its_two_figures():
    """"Once per floor" and "half health" are the row's own; the trigger is a ruling.

    "Once per floor, all defeated enemies on that floor resurrect at half health in a
    sudden holy revival." DivineResurgenceHealthPercent is the half. If the row changes
    either, the rule must follow; if it stops saying ALL, the uncapped revival does not
    hold.
    """
    words = flat(rows()["Celestial_Divine_Resurgence"]["Description"])
    assert "Once per floor" in words, (
        "The Divine Resurgence row no longer says ONCE PER FLOOR. The rule raises one "
        "revival a floor and forgets it at the stairs. " + words)
    assert "all defeated enemies" in words, (
        "The row no longer says ALL defeated enemies. The rule raises every recorded "
        "death with no cap, ruled on that word. " + words)
    assert "half health" in words, (
        "The row no longer says HALF health. Check DivineResurgenceHealthPercent. "
        + words)
    assert constant("DivineResurgenceHealthPercent") == 50.0, (
        "DivineResurgenceHealthPercent no longer holds the row's half.")


def test_a_risen_creature_pays_nothing_and_a_wraith_is_risen():
    """The owner's decision of 2026-09-17, read out of the code no automation test drives.

    "A creature that is revived or resurrected is marked, and its second death drops no
    loot and grants no experience." The drop roll and the experience grant sit in
    `ACataclysmEnemyCharacter::HandleDeath`, which needs a possessed player, a loot table
    and an enemy score before it reaches them, so no automation test reaches the two
    payments. This reads the handler with its comments removed and requires each payment
    to be gated on the mark; and requires Vengeful Wraiths, ruled a revival on
    2026-09-23, to set the mark.
    """
    enemy = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
             / "CataclysmEnemyCharacter.cpp").read_text(encoding="utf-8")
    handler = "\n".join(
        line for line in body_of(enemy, "void ACataclysmEnemyCharacter::HandleDeath(")
        .splitlines() if not line.lstrip().startswith("//"))

    assert re.search(r"const bool bPays\s*=\s*PaysForItsDeath\(\);", handler), (
        "HandleDeath no longer asks PaysForItsDeath, so a creature brought back pays "
        "loot and experience a second time.")
    assert re.search(r"if\s*\(\s*bPays\s*\)\s*\{\s*UCataclysmDropSpawner::SpawnDropsFor\(",
                     handler), (
        "The drop roll in HandleDeath is no longer gated on bPays.")
    assert re.search(r"if\s*\(\s*State\s*&&\s*bPays\s*\)\s*\{\s*State->GrantExperience\(",
                     handler), (
        "The experience grant in HandleDeath is no longer gated on bPays.")

    game_mode = (EFFECTS_DIR / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    wraith = body_of(game_mode, "void ACataclysmDungeonGameMode::ApplyVengefulWraithFigures(")
    assert "Wraith->bRisenFromTheDead = true;" in "\n".join(
        line for line in wraith.splitlines() if not line.lstrip().startswith("//")), (
        "A Vengeful Wraith is no longer marked as risen, so it pays twice for one kill.")


def test_dead_rising_row_still_states_a_chance_and_names_no_killer():
    """The two readings the rule rests on: no figure, and no killer.

    "Enemies have a chance to revive after being killed." THE ROW GIVES NO PERCENTAGE, so
    the rule's chance is the table's ten for a chance on a death; if the row ever states
    a figure, the rule must use that one. AND IT NAMES NO KILLER, which is why every death
    rolls whoever dealt it -- where Vengeful Wraiths says "the one who killed them" and so
    asks for the player. Both rulings were made under the owner's delegation on 2026-09-23.
    """
    words = flat(rows()["Death_Dead_Rising"]["Description"])
    lower = words.lower()

    assert "a chance" in lower, words
    assert "%" not in words, (
        "Death_Dead_Rising now states a percentage. The rule's chance was a judgement "
        "made because the row gave none -- see DeadRisingChancePercent in "
        "CataclysmDungeonModifierEffects.h. Use the row's figure instead. " + words)
    assert "after being killed" in lower, words
    assert "player" not in lower and "who killed" not in lower, (
        "Death_Dead_Rising now names who does the killing. Every death rolls because "
        "it named nobody; re-read the rule's killer. " + words)


def test_dead_rising_chance_is_declared_as_the_tables_figure_for_a_death():
    """The tie, not the number: one figure held by three rules cannot drift apart."""
    text = EFFECTS_HEADER.read_text(encoding="utf-8")

    assert re.search(
        r"DeadRisingChancePercent\s*=\s*SporeCloudsChancePercentOnDeath\s*;", text), (
        "DeadRisingChancePercent is no longer declared as SporeCloudsChancePercentOnDeath, "
        "the ten this table uses for a chance fired by a death. Write the tie back rather "
        "than a second number.")


def test_suffering_aura_row_still_names_health_and_mana_and_no_figure():
    """The row the rule reads: two pools it drains, one it cannot, and no rate.

    "The dungeon passively saps the player's resources (e.g., health, mana, stamina) at a
    slow but constant rate." Health and mana are what the rule drains; stamina is named
    and does not exist in this game; and the row gives no figure, which is why the rates
    are the project owner's decision of 2026-09-23. If the row ever states a figure or
    stops naming a pool, the rule must follow it.
    """
    words = flat(rows()["Famine_Suffering_Aura"]["Description"])
    lower = words.lower()

    for pool in ("health", "mana", "stamina"):
        assert pool in lower, (
            f"Famine_Suffering_Aura no longer names {pool}. The rule drains health and mana "
            "and nothing else, ruled on this list. " + words)
    assert "constant" in lower, (
        "Famine_Suffering_Aura no longer says CONSTANT. The rule takes the same share on "
        "every floor because it did. " + words)
    assert "%" not in words, (
        "Famine_Suffering_Aura now states a percentage. The rates were the owner's decision "
        "because the row gave none; use the row's figure. " + words)


def test_suffering_aura_beats_every_classs_base_regeneration():
    """The owner's intent of 2026-09-23, recomputed from the class table.

    The owner chose a drain the player notices, enough to beat every class's BASE
    regeneration. This reads game/Data/ClassStats.csv the way `UCataclysmClassStats::
    BaseFor` does -- a class's own row, else the Default row; Base plus PerLevel for each
    level above the first -- over every class and every level from 1 to 100, and requires
    each rate to exceed the highest regeneration share it finds. A class added or retuned
    to regenerate faster fails this, rather than quietly making the rule invisible.
    """
    with (REPO_ROOT / "game" / "Data" / "ClassStats.csv").open(
            newline="", encoding="utf-8") as handle:
        table = list(csv.DictReader(handle))
    classes = sorted({row["ClassName"] for row in table})

    def base(name: str, stat: str, level: int) -> float:
        row = next((r for r in table if r["ClassName"] == name and r["Stat"] == stat),
                   None) or next(r for r in table
                                 if r["ClassName"] == "Default" and r["Stat"] == stat)
        return float(row["Base"]) + float(row["PerLevel"]) * (level - 1)

    for pool, rate_name in (("health", "SufferingAuraHealthPercentPerSecond"),
                            ("mana", "SufferingAuraManaPercentPerSecond")):
        highest, who = max(
            (100.0 * base(name, f"{pool}_regen", level) / base(name, f"max_{pool}", level),
             f"{name} level {level}")
            for name in classes for level in range(1, 101))
        rate = constant(rate_name)
        assert rate > highest, (
            f"{rate_name} is {rate:g}% a second, and {who} regenerates {highest:.3f}% of its "
            f"maximum {pool} a second from its base alone. The owner asked on 2026-09-23 for a "
            "drain that beats every class's base regeneration; re-measure and move the rate.")


def test_blood_gates_row_still_seals_the_next_level_for_the_players_kills():
    """The four phrases the rule's readings rest on.

    "Doors leading to the next level in a dungeon are sealed shut until the player has
    slain enough enemies to open them." SEALED is the rule; THE NEXT LEVEL is why the last
    floor's way out is not sealed; THE PLAYER HAS SLAIN is why only the player's kills
    count; and ENOUGH gives no figure, which is why the half is a ruling. If any of them
    changes, the reading built on it must be revisited.
    """
    words = flat(rows()["Demonic_Blood_Gates"]["Description"])
    lower = words.lower()

    for phrase in ("sealed", "the next level", "the player has slain", "enough"):
        assert phrase in lower, (
            f"Demonic_Blood_Gates no longer says {phrase.upper()!r}. A reading of the rule "
            "rests on it; see BloodGatesKey in CataclysmDungeonModifierEffects.h. " + words)
    assert "%" not in words, (
        "Demonic_Blood_Gates now states a percentage. The half was a ruling because the row "
        "gave none; use the row's figure. " + words)


def test_dirge_resonance_row_still_states_ten_seconds_for_all_enemies():
    """The row's own figure and scope, and the two halves that grant nothing today.

    "Distant funeral music plays; when it crescendos, all enemies gain haste and fear
    immunity for 10 seconds." The ten seconds is `DirgeResonanceHasteSeconds`; ALL ENEMIES
    is why every living creature on the floor is hasted, marked ones included; and the
    period is a ruling, because the row states none. FEAR IMMUNITY and the MUSIC are in the
    row and do nothing, because this game has no fear and no audio for it -- if the row
    drops either, the note saying so should go too.
    """
    words = flat(rows()["Death_Dirge_Resonance"]["Description"])
    lower = words.lower()

    assert "10 seconds" in lower, (
        "Death_Dirge_Resonance no longer says 10 SECONDS. Check DirgeResonanceHasteSeconds. "
        + words)
    assert constant("DirgeResonanceHasteSeconds") == 10.0, (
        "DirgeResonanceHasteSeconds no longer holds the row's ten seconds.")
    for phrase in ("all enemies", "haste", "fear immunity", "music"):
        assert phrase in lower, (
            f"Death_Dirge_Resonance no longer says {phrase.upper()!r}; a reading of the rule "
            "rests on it. See DirgeResonanceKey in CataclysmDungeonModifierEffects.h. " + words)
    assert not re.search(r"every\s+\d+", lower), (
        "Death_Dirge_Resonance now states how often it crescendos. The ninety seconds was a "
        "ruling because it gave no period; use the row's. " + words)


def test_scarcity_row_still_disables_one_non_weapon_slots_stats_and_enchantments():
    """The row's own words, each of which a part of the rule rests on.

    "At the start of each floor, a random equipment slot (excluding weapons) has its stats
    and enchantments disabled for that floor." EACH FLOOR is when the draw happens; A
    RANDOM EQUIPMENT SLOT is what is drawn, which is why the slot and not the item is off;
    EXCLUDING WEAPONS is why the two weapon slots are never drawn; and STATS AND
    ENCHANTMENTS is why the item gives nothing at all and is no piece of a set.
    """
    words = flat(rows()["Famine_Scarcity"]["Description"])
    lower = words.lower()

    for phrase in ("each floor", "a random equipment slot", "excluding weapons",
                   "stats and enchantments", "for that floor"):
        assert phrase in lower, (
            f"Famine_Scarcity no longer says {phrase.upper()!r}; a part of the rule rests on "
            "it. See ScarcityKey in CataclysmDungeonModifierEffects.h. " + words)


def test_chaotic_loot_row_still_names_enemy_drops_and_stats_and_the_cap_still_stands():
    """The row's scope, and the design sentence the ruling rests on.

    "Items dropped by enemies have randomized stats within a wide range, making each piece
    potentially extremely valuable or useless." DROPPED BY ENEMIES is why nothing crafted
    or owned changes; STATS is why rarity and the number of affixes do not. The cap stays
    because docs/Cataclysm_GDD_v2.md says "The affix tier column IS still a hard cap."
    If that sentence goes, the ruling of 2026-09-23 has lost its ground and should be put
    again rather than left standing.
    """
    words = flat(rows()["Chaos_Chaotic_Loot"]["Description"])
    lower = words.lower()

    for phrase in ("items dropped by enemies", "randomized stats", "a wide range"):
        assert phrase in lower, (
            f"Chaos_Chaotic_Loot no longer says {phrase.upper()!r}; a part of the rule rests "
            "on it. See ChaoticLootKey in CataclysmDungeonModifierEffects.h. " + words)

    design = (REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md").read_text(encoding="utf-8")
    assert "The affix tier column IS still a hard cap." in design, (
        "docs/Cataclysm_GDD_v2.md no longer says the affix tier column is a hard cap. "
        "Chaotic Loot keeps UCataclysmDropRoll::MaxAffixTierOnADrop as its cap on that "
        "sentence alone; put the ruling again.")


def test_unstable_portal_row_still_states_its_three_odds_and_the_warden_is_the_mini_boss():
    """The row's three figures, held against the two constants, and the design's mini-boss.

    "Stepping through a portal has a 50% chance of taking you to the next floor, a 25%
    chance of returning you to the beginning of the current floor, and a 25% chance of
    spawning a powerful, unpredictable mini-boss." UnstablePortalDescendPercent is the
    first figure and UnstablePortalReturnBelow the first two added. The mini-boss is an
    Abyssal Warden because docs/Cataclysm_GDD_v2.md names it the mini-boss.
    """
    words = flat(rows()["Chaos_Unstable_Portal"]["Description"])
    percents = [float(p) for p in re.findall(r"(\d+(?:\.\d+)?)\s*%", words)]

    assert len(percents) == 3, (
        f"Chaos_Unstable_Portal no longer states three chances: found {percents} in: {words}")
    assert constant("UnstablePortalDescendPercent") == percents[0], (
        f"UnstablePortalDescendPercent no longer holds the row's {percents[0]:g}% down.")
    assert constant("UnstablePortalReturnBelow") == percents[0] + percents[1], (
        "UnstablePortalReturnBelow no longer holds the row's first two chances added: "
        f"{percents[0] + percents[1]:g}.")
    assert sum(percents) == 100.0, f"The row's three chances no longer add to 100: {percents}"
    assert "mini-boss" in words.lower(), words

    design = (REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md").read_text(encoding="utf-8")
    assert "The Abyssal Warden (Mini-Boss)" in design, (
        "docs/Cataclysm_GDD_v2.md no longer names the Abyssal Warden as the mini-boss. The "
        "unstable portal raises one on that line alone; put the ruling again.")

def test_nothing_is_forgotten_row_still_feeds_the_final_boss_a_portion_of_the_players_kills():
    """The four phrases the rule's readings rest on.

    "Enemies that the player kills aren't forgotten, instead a portion of their stats are
    fed back into the void to fuel the final boss of the dungeon." THE PLAYER KILLS is why
    only the player's kills feed it; A PORTION gives no figure, which is why the five per
    cent is a ruling; THEIR STATS is read as maximum health and attack damage; and THE
    FINAL BOSS OF THE DUNGEON is why only the last floor's Gatekeeper is fed. If any of
    them changes, the reading built on it must be revisited.
    """
    words = flat(rows()["Void_Nothing_Is_Forgotten"]["Description"])
    lower = words.lower()

    for phrase in ("the player kills", "a portion", "their stats", "the final boss of the dungeon"):
        assert phrase in lower, (
            f"Void_Nothing_Is_Forgotten no longer says {phrase.upper()!r}. A reading of the "
            "rule rests on it; see NothingIsForgottenKey in CataclysmDungeonModifierEffects.h. "
            + words)
    assert "%" not in words, (
        "Void_Nothing_Is_Forgotten now states a percentage. The five per cent was a ruling "
        "because the row gave none; use the row's figure. " + words)

def test_starvation_curse_row_still_adds_one_of_two_debuffs_each_floor_until_cleansed():
    """The phrases the rule's readings rest on.

    "Each new floor adds a starvation debuff, such as slower movement or reduced max
    health. These debuffs persist unless cleansed." EACH NEW FLOOR is why every floor
    carrying the row adds one stack; SLOWER MOVEMENT and REDUCED MAX HEALTH are the only
    two kinds built; PERSIST UNLESS CLEANSED is why the stacks outlast the stairs and a
    floor's boss clears them; and the row gives no figure, which is why five per cent a
    stack and ten stacks are rulings. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Famine_Starvation_Curse"]["Description"])
    lower = words.lower()

    for phrase in ("each new floor", "slower movement", "reduced max health",
                   "persist unless cleansed"):
        assert phrase in lower, (
            f"Famine_Starvation_Curse no longer says {phrase.upper()!r}. A reading of the "
            "rule rests on it; see StarvationCurseKey in CataclysmDungeonModifierEffects.h. "
            + words)
    assert "%" not in words, (
        "Famine_Starvation_Curse now states a percentage. Five per cent a stack was a ruling "
        "because the row gave none; use the row's figure. " + words)

def test_trick_or_treat_row_still_raises_enemies_or_buffs_on_picking_up_loot():
    """The phrases the rule's readings rest on.

    "Picking up loot spawns additional enemies or applies temporary buffs to the player."
    PICKING UP LOOT is why a take fires it; SPAWNS ADDITIONAL ENEMIES and TEMPORARY BUFFS are
    its two outcomes; OR is why a pickup gets one of them, at even odds; and the row gives
    no figure, which is why two creatures, twenty per cent and ten seconds are rulings. If
    any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Chaos_Trick_or_Treat"]["Description"])
    lower = words.lower()

    for phrase in ("picking up loot", "spawns additional enemies", " or ",
                   "temporary buffs to the player"):
        assert phrase in lower, (
            f"Chaos_Trick_or_Treat no longer says {phrase.strip().upper()!r}. A reading of "
            "the rule rests on it; see TrickOrTreatKey in CataclysmDungeonModifierEffects.h. "
            + words)
    assert "%" not in words, (
        "Chaos_Trick_or_Treat now states a percentage. The even odds and the haste were "
        "rulings because the row gave none; use the row's figure. " + words)

def test_soul_harvest_row_still_feeds_the_nearest_demon_health_damage_and_resistances():
    """The phrases the rule's readings rest on.

    "Defeated enemies release demonic souls that empower other enemies nearby. Souls float
    toward the nearest demon, granting increased health, damage, and resistances." DEFEATED
    ENEMIES is why every death releases one, whoever killed it; NEARBY and THE NEAREST DEMON
    are why it goes to the nearest living creature within a radius; HEALTH, DAMAGE, AND
    RESISTANCES are the three things a soul raises; and the row gives no figure, which is why
    ten per cent, five points and five souls are rulings. If any of them changes, the reading
    must be revisited.
    """
    words = flat(rows()["Demonic_Soul_Harvest"]["Description"])
    lower = words.lower()

    for phrase in ("defeated enemies", "nearby", "the nearest demon",
                   "health, damage, and resistances"):
        assert phrase in lower, (
            f"Demonic_Soul_Harvest no longer says {phrase.upper()!r}. A reading of the rule "
            "rests on it; see SoulHarvestKey in CataclysmDungeonModifierEffects.h. " + words)
    assert "%" not in words, (
        "Demonic_Soul_Harvest now states a percentage. The figures a soul gives were rulings "
        "because the row gave none; use the row's figure. " + words)

def test_chaos_touched_row_still_adds_a_random_buff_or_debuff_each_floor_until_cleansed():
    """The phrases the rule's readings rest on.

    "Every floor, a random buff or debuff is added to the player. These do not have the
    normal time limits and will continue to stack unless cleansed." EVERY FLOOR is why each
    floor adds one; A RANDOM BUFF OR DEBUFF is why the draw spans both; CONTINUE TO STACK is
    why stacks add; UNLESS CLEANSED is why a floor's boss clears the debuffs; and the row
    gives no figure, which is why ten per cent and five of a kind are rulings. If any of them
    changes, the reading must be revisited.
    """
    words = flat(rows()["Chaos_Chaos_Touched"]["Description"])
    lower = words.lower()

    for phrase in ("every floor", "a random buff or debuff", "continue to stack",
                   "unless cleansed"):
        assert phrase in lower, (
            f"Chaos_Chaos_Touched no longer says {phrase.upper()!r}. A reading of the rule "
            "rests on it; see ChaosTouchedKey in CataclysmDungeonModifierEffects.h. " + words)
    assert "%" not in words, (
        "Chaos_Chaos_Touched now states a percentage. Ten per cent a stack was a ruling "
        "because the row gave none; use the row's figure. " + words)

def test_the_reaper_row_still_stalks_slowly_and_kills_with_one_hit_of_the_scythe():
    """The phrases the rule's readings rest on.

    "The embodiment of death slowly stalks the player. If they are hit by his scythe, they
    instantly die." SLOWLY is why the Reaper is a creature slower than every class; STALKS is
    why it notices the player from anywhere on the floor; HIT BY HIS SCYTHE is why only its
    own landed blow kills; INSTANTLY DIE is why the kill goes straight to health, past
    Nothing Stops It; and the row gives no figure, which is why ten seconds is a ruling. If
    any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Death_The_Reaper"]["Description"])
    lower = words.lower()

    for phrase in ("slowly stalks the player", "hit by his scythe", "instantly die"):
        assert phrase in lower, (
            f"Death_The_Reaper no longer says {phrase.upper()!r}. A reading of the rule rests "
            "on it; see TheReaperKey in CataclysmDungeonModifierEffects.h. " + words)
    assert not any(character.isdigit() for character in words), (
        "Death_The_Reaper now states a figure. Ten seconds into a floor was a ruling because "
        "the row gave none; use the row's figure. " + words)

def test_blood_bond_row_still_binds_the_first_elite_seen_until_the_player_dies():
    """The phrases the rule's readings rest on.

    "You are soul-linked to the first elite you see on each floor. This enemy cannot die unless
    you do and is immune to your damage." THE FIRST ELITE is why exactly one creature at the
    Elite rung is bound; YOU SEE is why it is the one that notices the player; EACH FLOOR is why
    a bond is one a floor and a floor change releases it; CANNOT DIE UNLESS YOU DO is why the
    player's death kills it; IMMUNE TO YOUR DAMAGE is why it cannot be hurt; and the row gives
    no figure. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Demonic_Blood_Bond"]["Description"])
    lower = words.lower()

    for phrase in ("the first elite you see", "on each floor", "cannot die unless you do",
                   "immune to your damage"):
        assert phrase in lower, (
            f"Demonic_Blood_Bond no longer says {phrase.upper()!r}. A reading of the rule rests "
            "on it; see BloodBondKey in CataclysmDungeonModifierEffects.h. " + words)
    assert not any(character.isdigit() for character in words), (
        "Demonic_Blood_Bond now states a figure; the rule states none. " + words)

def test_plague_convergence_row_still_converges_after_too_long_with_an_exponential_disease():
    """The phrases the rule's readings rest on.

    "If players spend too long on a floor, the dungeon begins to "converge" on them. Hordes of
    enemies will spawn continuously, all carrying highly contagious diseases that stack
    exponentially with every hit. The only way to stop the convergence is to complete objectives
    quickly and descend to the next floor." TOO LONG ON A FLOOR is why it begins on a clock;
    SPAWN CONTINUOUSLY is why waves come on a cadence; STACK EXPONENTIALLY WITH EVERY HIT is why
    each landed blow doubles the disease; DESCEND TO THE NEXT FLOOR is why only a floor change
    stops it; and the row gives no figure, which is why every number is a ruling. If any of
    them changes, the reading must be revisited.
    """
    words = flat(rows()["Pestilence_Plague_Convergence"]["Description"])
    lower = words.lower()

    for phrase in ("spend too long on a floor", "spawn continuously",
                   "stack exponentially with every hit", "descend to the next floor"):
        assert phrase in lower, (
            f"Pestilence_Plague_Convergence no longer says {phrase.upper()!r}. A reading of the "
            "rule rests on it; see PlagueConvergenceKey in CataclysmDungeonModifierEffects.h. "
            + words)
    assert not any(character.isdigit() for character in words), (
        "Pestilence_Plague_Convergence now states a figure; the rule's figures are rulings. "
        + words)

def test_divine_wrath_row_still_sends_chasing_beams_that_destroy_enemies_and_hurt_players():
    """The phrases the rule's readings rest on.

    "Players are periodically targeted by massive beams of radiant light that chase them across
    the floor. These beams destroy enemies in their path but deal devastating damage to players
    if they fail to avoid them." PERIODICALLY is why a beam comes on a cadence; CHASE is why it is
    aimed at the player on every beat; DESTROY ENEMIES IN THEIR PATH is why it kills the creatures
    it covers; FAIL TO AVOID is why it is slower than every class; and the row gives no figure,
    which is why every number is a ruling. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Celestial_Divine_Wrath"]["Description"])
    lower = words.lower()

    for phrase in ("periodically targeted", "chase them across the floor",
                   "destroy enemies in their path", "fail to avoid them"):
        assert phrase in lower, (
            f"Celestial_Divine_Wrath no longer says {phrase.upper()!r}. A reading of the rule rests "
            "on it; see DivineWrathKey in CataclysmDungeonModifierEffects.h. " + words)
    assert not any(character.isdigit() for character in words), (
        "Celestial_Divine_Wrath now states a figure; the rule's figures are rulings. " + words)


def test_a_divine_wrath_beam_is_slower_than_every_class():
    """"Fail to avoid them" is only fair if a player who keeps moving escapes, so the beam's speed
    must stay below every class's movement speed.

    Read from the two places each number lives: `DivineWrathSpeedCmPerSecond` in the header, in
    centimetres a second, and `movement_speed` in `game/Data/ClassStats.csv`, in metres a second. A
    class with no row of its own takes the `Default` row, as `UCataclysmClassStats::BaseFor` does.
    """
    match = re.search(r"DivineWrathSpeedCmPerSecond\s*=\s*([0-9.]+)f",
                      EFFECTS_HEADER.read_text(encoding="utf-8"))
    assert match, "DivineWrathSpeedCmPerSecond was not found in the header."
    beam_metres = float(match.group(1)) / 100.0

    table = REPO_ROOT / "game" / "Data" / "ClassStats.csv"
    with table.open(encoding="utf-8", newline="") as handle:
        stats = list(csv.DictReader(handle))
    speeds = {row["ClassName"]: float(row["Base"]) for row in stats
              if row["Stat"] == "movement_speed"}
    classes = {row["ClassName"] for row in stats} - {"Default"}
    assert classes, "ClassStats.csv names no class."
    for name in sorted(classes):
        speed = speeds.get(name, speeds.get("Default"))
        assert speed is not None, f"{name} has no movement speed, and neither does Default."
        assert beam_metres < speed, (
            f"A Divine Wrath beam moves at {beam_metres} m/s and {name} at {speed} m/s, so a "
            f"{name} who keeps moving cannot escape it. The row says players take damage when they "
            "\"fail to avoid them\"; slow the beam or rule again.")


def test_echoes_of_the_past_row_still_brings_back_the_previous_floors_dead_for_one_attack():
    """The phrases the rule's readings rest on.

    "Spectral versions of enemies killed on the previous floor appear and repeat their final
    attacks before vanishing." SPECTRAL is why an echo cannot be hurt and pays nothing; KILLED ON
    THE PREVIOUS FLOOR is why deaths are recorded on every floor and handed over one floor on;
    REPEAT THEIR FINAL ATTACKS is why each creature's last attack is recorded and made once; BEFORE
    VANISHING is why they go a second later; and the row gives no figure, which is why every number
    is a ruling. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Death_Echoes_of_the_Past"]["Description"])
    lower = words.lower()

    for phrase in ("spectral versions", "killed on the previous floor",
                   "repeat their final attacks", "before vanishing"):
        assert phrase in lower, (
            f"Death_Echoes_of_the_Past no longer says {phrase.upper()!r}. A reading of the rule "
            "rests on it; see EchoesOfThePastKey in CataclysmDungeonModifierEffects.h. " + words)
    assert not any(character.isdigit() for character in words), (
        "Death_Echoes_of_the_Past now states a figure; the rule's figures are rulings. " + words)


def test_plague_harbingers_row_still_has_harbingers_lay_trails_that_their_deaths_cleanse():
    """The phrases the rule's readings rest on.

    CERTAIN ENEMIES is why only some creatures are Harbingers; WHEREVER THEY WALK is why a patch is
    laid as a Harbinger moves; LINGER is why a patch lasts the floor; DAMAGING PLAYERS and AMPLIFYING
    NEARBY ENEMY STATS are the two things a patch does; CLEANSES THE TRAILS and WEAKENS NEARBY
    ENEMIES are what a Harbinger's death does. The row gives no figure, which is why every number is
    a ruling. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Pestilence_Plague_Harbingers"]["Description"])
    lower = words.lower()

    for phrase in ("certain enemies", "wherever they walk", "trails linger",
                   "damaging players who cross them", "amplifying nearby enemy stats",
                   "cleanses the trails", "weakens nearby enemies"):
        assert phrase in lower, (
            f"Pestilence_Plague_Harbingers no longer says {phrase.upper()!r}. A reading of the rule "
            "rests on it; see PlagueHarbingersKey in CataclysmDungeonModifierEffects.h. " + words)
    assert not any(character.isdigit() for character in words), (
        "Pestilence_Plague_Harbingers now states a figure; the rule's figures are rulings. " + words)


def test_wings_of_the_host_row_still_carpet_bombs_with_feathers_that_pierce_terrain():
    """The phrases the rule's readings rest on.

    "Angelic flyovers carpet-bomb the map with radiant feathers that pierce terrain and deal % max HP
    damage." FLYOVERS is why a flyover comes on a cadence; CARPET-BOMB THE MAP is why its feathers lie
    along a line across the whole floor; PIERCE TERRAIN is why a wall protects nothing, which is
    already true of every area hit in this game; % MAX HP is the shape of the damage; the row gives
    no figure, which is why every number is a ruling. It names no creature, which is why feathers
    strike the player only. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Celestial_Wings_of_the_Host"]["Description"])
    lower = words.lower()

    for phrase in ("flyovers", "carpet-bomb the map", "radiant feathers", "pierce terrain",
                   "% max hp damage"):
        assert phrase in lower, (
            f"Celestial_Wings_of_the_Host no longer says {phrase.upper()!r}. A reading of the rule "
            "rests on it; see WingsOfTheHostKey in CataclysmDungeonModifierEffects.h. " + words)
    for creature in ("enem", "monster", "creature"):
        assert creature not in lower, (
            f"Celestial_Wings_of_the_Host now names {creature!r}; feathers strike the player only "
            "because the row named no creature. Rule again. " + words)
    assert not any(character.isdigit() for character in words), (
        "Celestial_Wings_of_the_Host now states a figure; the rule's figures are rulings. " + words)

def test_eternal_chorus_row_still_halves_resource_regeneration_until_its_source_is_destroyed():
    """The phrases the rule's readings rest on.

    "Certain areas resonate with a haunting celestial hymn. While within earshot of the chorus, all
    cooldowns are increased, and resource regeneration is halved. Players must destroy the source of
    the hymn to silence it." CERTAIN AREAS is why choruses stand at places; WITHIN EARSHOT is why the
    effects hold only inside a radius; ALL COOLDOWNS ARE INCREASED is the lengthening; RESOURCE
    REGENERATION IS HALVED is the row's one figure and why health regeneration is untouched; DESTROY
    THE SOURCE is why each chorus has a creature to kill. If any of them changes, the reading must be
    revisited.
    """
    words = flat(rows()["Celestial_Eternal_Chorus"]["Description"])
    lower = words.lower()

    for phrase in ("certain areas", "within earshot", "all cooldowns are increased",
                   "resource regeneration is halved", "destroy the source"):
        assert phrase in lower, (
            f"Celestial_Eternal_Chorus no longer says {phrase.upper()!r}. A reading of the rule "
            "rests on it; see EternalChorusKey in CataclysmDungeonModifierEffects.h. " + words)


def test_necrotic_bloom_row_still_sends_waves_every_twenty_seconds_until_its_flowers_are_destroyed():
    """The phrases the rule's readings rest on.

    "Cursed flowers sprout in random areas; if not destroyed, they spawn waves of undead every 20s."
    FLOWERS SPROUT IN RANDOM AREAS is why flowers stand at random places; IF NOT DESTROYED is why each is a
    creature to kill; WAVES OF UNDEAD is what they send, read as Grave Tide reads it; EVERY 20S is the
    row's one figure. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Death_Necrotic_Bloom"]["Description"])
    lower = words.lower()

    for phrase in ("flowers sprout in random areas", "if not destroyed", "waves of undead", "every 20s"):
        assert phrase in lower, (
            f"Death_Necrotic_Bloom no longer says {phrase.upper()!r}. A reading of the rule rests on it; "
            "see NecroticBloomKey in CataclysmDungeonModifierEffects.h. " + words)


def test_golden_spires_row_still_heals_and_strengthens_enemies_until_its_spires_are_destroyed():
    """The phrases the rule's readings rest on.

    "Floors feature radiant towers that heal enemies and buff their damage. These spires must be
    destroyed to progress effectively." RADIANT TOWERS is why spires stand at places; HEAL ENEMIES is
    Field Medic's heal; BUFF THEIR DAMAGE is the damage multiplier; MUST BE DESTROYED is why each is a
    creature to kill; PROGRESS EFFECTIVELY is why nothing locks the way out. If any of them changes,
    the reading must be revisited.
    """
    words = flat(rows()["Celestial_Golden_Spires"]["Description"])
    lower = words.lower()

    for phrase in ("radiant towers", "heal enemies", "buff their damage", "must be destroyed",
                   "progress effectively"):
        assert phrase in lower, (
            f"Celestial_Golden_Spires no longer says {phrase.upper()!r}. A reading of the rule rests "
            "on it; see GoldenSpiresKey in CataclysmDungeonModifierEffects.h. " + words)


def test_pestilent_empowerment_row_still_strengthens_later_floors_until_its_beacons_are_destroyed():
    """The phrases the rule's readings rest on.

    "Players must find and destroy the plague beacons on each floor if they want to lower the power of
    enemies on later floors." DESTROY THE PLAGUE BEACONS is why each beacon is a creature to kill; ON
    EACH FLOOR is why a floor has its own; LOWER THE POWER OF ENEMIES is read as keeping it lower than a
    standing beacon would make it; ON LATER FLOORS is why the count is carried and never applies to the
    floor a beacon stands on. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Pestilence_Pestilent_Empowerment"]["Description"])
    lower = words.lower()

    for phrase in ("destroy the plague beacons", "on each floor", "lower the power of enemies",
                   "on later floors"):
        assert phrase in lower, (
            f"Pestilence_Pestilent_Empowerment no longer says {phrase.upper()!r}. A reading of the rule "
            "rests on it; see PestilentEmpowermentKey in CataclysmDungeonModifierEffects.h. " + words)


def test_infested_veins_row_still_poisons_the_ground_grows_back_and_calls_guardians():
    """The phrases the rule's readings rest on.

    "Living tunnels and walls pulsate with veins of infectious growths that create a toxic environment.
    Players can choose to destroy these veins to temporarily cleanse the area, but destroying too much
    summons toxic "guardians" from the infection." TUNNELS AND WALLS is why veins stand beside walls;
    TOXIC ENVIRONMENT is the zone that burns; DESTROY THESE VEINS is why each is a creature to kill;
    TEMPORARILY CLEANSE is why a vein grows back; DESTROYING TOO MUCH and GUARDIANS are the threshold and
    what it calls. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Pestilence_Infested_Veins"]["Description"])
    lower = words.lower()

    for phrase in ("tunnels and walls", "toxic environment", "destroy these veins", "temporarily cleanse",
                   "destroying too much", "guardians"):
        assert phrase in lower, (
            f"Pestilence_Infested_Veins no longer says {phrase.upper()!r}. A reading of the rule rests on "
            "it; see InfestedVeinsKey in CataclysmDungeonModifierEffects.h. " + words)


def test_trial_of_endurance_row_still_doubles_enemies_when_the_floor_is_not_cleared_in_time():
    """The phrases the rule's readings rest on.

    "A divine timer per floor; if it expires before the floor is cleared, all enemies gain doubled damage
    and resistances." A DIVINE TIMER PER FLOOR is the clock each floor starts; BEFORE THE FLOOR IS CLEARED
    is why clearing stops it; DOUBLED DAMAGE AND RESISTANCES is the row's one figure. If any of them
    changes, the reading must be revisited.
    """
    words = flat(rows()["Celestial_Trial_of_Endurance"]["Description"])
    lower = words.lower()

    for phrase in ("a divine timer per floor", "before the floor is cleared",
                   "doubled damage and resistances"):
        assert phrase in lower, (
            f"Celestial_Trial_of_Endurance no longer says {phrase.upper()!r}. A reading of the rule rests "
            "on it; see TrialOfEnduranceKey in CataclysmDungeonModifierEffects.h. " + words)


def test_void_parasite_row_still_leaves_voidlings_that_attach_and_light_that_clears_them():
    """The phrases the rule's readings rest on.

    "Every enemy you kill has a chance to spawn a parasitic voidling. If the voidling reaches you, it will attach
    to you and siphon your power, reducing your damage, resistances, and movement speed. The voidling can be
    removed by standing in a "light" zone, but these are rare." EVERY ENEMY YOU KILL is why only the player's kill
    counts; A CHANCE is the roll; REACHES YOU and ATTACH are the distance at which it becomes a stack; DAMAGE,
    RESISTANCES and MOVEMENT SPEED are the stats each stack takes from; STANDING IN and LIGHT are the zone that
    clears them; RARE is why there is one a floor. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Void_Void_Parasite"]["Description"])
    lower = words.lower()

    for phrase in ("every enemy you kill", "a chance", "reaches you", "attach", "your damage, resistances, and "
                   "movement speed", "standing in", "light", "rare"):
        assert phrase in lower, (
            f"Void_Void_Parasite no longer says {phrase.upper()!r}. A reading of the rule rests on it; see "
            "VoidParasiteKey in CataclysmDungeonModifierEffects.h. " + words)


def test_obsidian_sarcophagi_row_still_grants_damage_and_resistance_and_lets_out_a_vampire_lord():
    """The phrases the rule's readings rest on.

    "Indestructible coffins pulse with death magic, granting enemies in range bonus damage and resistance. Once
    enough nearby enemies have been slain, a Vampire Lord erupts from the coffin to kill the player."
    INDESTRUCTIBLE is why a coffin cannot be hurt; IN RANGE is the radius; BONUS DAMAGE AND RESISTANCE are the
    two bonuses; ENOUGH NEARBY ENEMIES HAVE BEEN SLAIN is the count of deaths beside it; VAMPIRE LORD and TO
    KILL THE PLAYER are the creature it lets out and why it hunts. If any of them changes, the reading must be
    revisited.
    """
    words = flat(rows()["Death_Obsidian_Sarcophagi"]["Description"])
    lower = words.lower()

    for phrase in ("indestructible", "in range", "bonus damage and resistance", "enough nearby enemies have been slain",
                   "vampire lord", "to kill the player"):
        assert phrase in lower, (
            f"Death_Obsidian_Sarcophagi no longer says {phrase.upper()!r}. A reading of the rule rests on it; see "
            "ObsidianSarcophagiKey in CataclysmDungeonModifierEffects.h. " + words)


def test_every_rule_writes_resistance_through_the_rule_resistance_record_under_its_own_key():
    """Trial of Endurance and Obsidian Sarcophagi each write a creature's all-resistance through
    `SetRuleResistance`, under keys of their own.

    A rule that wrote the base directly would be counted by the record as another writer's change and folded
    into the creature's own figure, so the next rule would multiply it. Ruled 2026-09-25.
    """
    mode = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
            / "CataclysmDungeonGameMode.cpp").read_text(encoding="utf-8")
    header = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Dungeon"
              / "CataclysmDungeonGameMode.h").read_text(encoding="utf-8")
    keys = {}
    for step, key in (("StepTrialOfEndurance", "TrialOfEnduranceResistanceSource"),
                      ("StepObsidianSarcophagi", "ObsidianSarcophagiResistanceSource")):
        body = body_of(mode, f"void ACataclysmDungeonGameMode::{step}(")
        assert f"SetRuleResistance(Creature, {key}," in body, (
            f"{step} no longer writes resistance through SetRuleResistance under its own key, {key}.")
        found = re.search(rf'{key}\s*=\s*TEXT\("(\w+)"\)', header)
        assert found, f"{key} is no longer declared in CataclysmDungeonGameMode.h."
        keys[key] = found.group(1)
    assert len(set(keys.values())) == len(keys), f"Two rules share a resistance key: {keys}"


def test_portal_unleashing_row_still_has_portals_that_keep_sending_creatures_to_be_dispatched():
    """The phrases the rule's readings rest on.

    "The dungeon is riddled with unstable portals that periodically spawn twisted abominations from the void.
    Players must swiftly dispatch these creatures before they overwhelm the party." UNSTABLE PORTALS is why a
    portal cannot be destroyed; PERIODICALLY SPAWN is its clock; TWISTED ABOMINATIONS is the stand-in it sends;
    SWIFTLY DISPATCH and BEFORE THEY OVERWHELM are why killing them is what holds it back, under a cap. If any of
    them changes, the reading must be revisited.
    """
    words = flat(rows()["Void_Portal_Unleashing"]["Description"])
    lower = words.lower()

    for phrase in ("unstable portals", "periodically spawn", "twisted abominations", "swiftly dispatch",
                   "before they overwhelm"):
        assert phrase in lower, (
            f"Void_Portal_Unleashing no longer says {phrase.upper()!r}. A reading of the rule rests on it; see "
            "PortalUnleashingKey in CataclysmDungeonModifierEffects.h. " + words)


def test_raw_sewage_row_still_has_rivers_that_give_disease_stacks_that_never_time_out():
    """The phrases the rule's readings rest on.

    "The dungeon is an actual cesspool, filled with rivers of toxic waste that will spread disease stacks to the
    player. These disease stacks do not time out and must be cleansed." RIVERS OF TOXIC WASTE is the lines of marks;
    DISEASE STACKS is why the player carries the disease keyword; DO NOT TIME OUT is why the stacks are the
    dungeon's; MUST BE CLEANSED is the floor boss's death. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Pestilence_Raw_Sewage"]["Description"])
    lower = words.lower()

    for phrase in ("rivers of toxic waste", "disease stacks", "do not time out", "must be cleansed"):
        assert phrase in lower, (
            f"Pestilence_Raw_Sewage no longer says {phrase.upper()!r}. A reading of the rule rests on it; see "
            "RawSewageKey in CataclysmDungeonModifierEffects.h. " + words)


def test_swarm_of_locusts_row_still_sweeps_obscures_burns_and_asks_for_shelter():
    """The phrases the rule's readings rest on.

    "Periodically, swarms of locusts sweep through the dungeon, obscuring vision and dealing continuous damage.
    Players must find shelter or use specific abilities to survive the swarm." PERIODICALLY is its clock; SWEEP
    THROUGH is its travel; CONTINUOUS DAMAGE is the burn once a second; FIND SHELTER is the shelters; OBSCURING
    VISION is the part not built, waiting on the vision system. If any of them changes, the reading must be revisited.
    """
    words = flat(rows()["Famine_Swarm_of_Locusts"]["Description"])
    lower = words.lower()

    for phrase in ("periodically", "sweep through", "obscuring vision", "continuous damage", "find shelter"):
        assert phrase in lower, (
            f"Famine_Swarm_of_Locusts no longer says {phrase.upper()!r}. A reading of the rule rests on it; see "
            "SwarmOfLocustsKey in CataclysmDungeonModifierEffects.h. " + words)
