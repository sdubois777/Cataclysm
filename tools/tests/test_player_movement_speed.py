"""The player must walk at a designed speed, not at Unreal's engine default.

WHY THIS EXISTS. Issue #391. `ACataclysmPlayerCharacter` configured its movement
component -- rotation rate, plane constraint, orient-to-movement -- and never
touched `MaxWalkSpeed`, so the player ran at
`UCharacterMovementComponent`'s own default of 600 cm/s. The design gives the
three Demonic classes 4.6, 3.5 and 4.0 metres per second and not one of them
reached the game. The fault survived for as long as the project has existed
because a character that moves looks like a character that is working.

WHAT MADE IT EXPENSIVE. Every judgement about closing and escaping was made
against 600. The Brute's chase speed of 500 was set by playing against it and
reads as a comfortable margin at 600 and as unescapable at 400.

WHY IT IS A PYTHON TEST. Continuous integration never opens the editor and never
builds the C++ -- `.github/workflows/ci.yml` is a single Linux job. The
automation tests in `game/Source/Cataclysm/Tests/CataclysmPlayerMovementTests.cpp`
check the same thing against a live actor and only ever run on a developer's
machine. This reads the source text, which is enough to catch the two ways it
goes wrong: the number drifting from the design data, and the assignment being
deleted so that the constant exists and nothing uses it.

WHICH IS AUTHORITATIVE. The Python model and the class stat data. When this
fails, the usual fix is to change the C++.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PLAYER_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                 / "CataclysmPlayerCharacter.h")
PLAYER_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
              / "CataclysmPlayerCharacter.cpp")
COMBAT_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
              / "CataclysmCombatAttributeSet.cpp")
CLASS_STATS_CSV = REPO_ROOT / "game" / "Data" / "ClassStats.csv"

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: `UCharacterMovementComponent::MaxWalkSpeed` in the engine's own constructor,
#: at Engine/Source/Runtime/Engine/Private/Components/
#: CharacterMovementComponent.cpp. This is the value the defect produced.
ENGINE_DEFAULT_WALK_SPEED_CM = 600.0

#: Metres to centimetres. The design and the model work in metres per second;
#: Unreal walks in centimetres per second.
CM_PER_METRE = 100.0


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def constant(header: pathlib.Path, name: str) -> float:
    """The value of a `static constexpr float <name> = <number>f;` line."""
    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        source(header),
    )
    if match is None:
        pytest.fail(
            f"{header.relative_to(REPO_ROOT)} has no "
            f"'static constexpr float {name} = <number>f;' line. If it was "
            f"renamed, rename it here too; if it was deleted, the player's walk "
            f"speed is unguarded again and the engine default can come back."
        )
    return float(match.group(1))


def class_stat(class_name: str, stat: str) -> float:
    """One `Base` figure from game/Data/ClassStats.csv."""
    if not CLASS_STATS_CSV.is_file():
        pytest.fail(f"{CLASS_STATS_CSV.relative_to(REPO_ROOT)} does not exist")

    with CLASS_STATS_CSV.open(encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            if row["ClassName"] == class_name and row["Stat"] == stat:
                return float(row["Base"])

    pytest.fail(
        f"game/Data/ClassStats.csv has no {stat} row for {class_name}. It is "
        f"generated from the design workbook, so either the sheet lost the row "
        f"or the generator stopped writing it."
    )


@pytest.fixture(scope="module")
def default_speed_cm() -> float:
    return constant(PLAYER_HEADER, "DefaultWalkSpeedCmPerSecond")


def test_the_player_does_not_walk_at_the_engine_default(default_speed_cm) -> None:
    """The whole of issue #391, stated as the one thing that must not be true."""
    assert default_speed_cm != pytest.approx(ENGINE_DEFAULT_WALK_SPEED_CM), (
        f"ACataclysmPlayerCharacter walks at {default_speed_cm:.0f} cm/s, which "
        f"is UCharacterMovementComponent's own default. That is the state issue "
        f"#391 describes: no designed class speed reaches the game, movement "
        f"speed as a stat does nothing, and every judgement about whether an "
        f"enemy can be escaped is being made against an engine constant."
    )


def test_the_default_is_the_shared_class_stat_line(default_speed_cm) -> None:
    """It is the `Default` row of the class stat data, not an invented number.

    That row is what a class inherits when it does not override movement speed,
    and there is no class selection yet, so it is what a character who has
    chosen nothing should walk at.
    """
    sheet_metres = class_stat("Default", "movement_speed")

    assert default_speed_cm == pytest.approx(sheet_metres * CM_PER_METRE), (
        f"CataclysmPlayerCharacter.h walks at {default_speed_cm:.0f} cm/s but "
        f"the Default line in game/Data/ClassStats.csv is {sheet_metres} metres "
        f"per second, which is {sheet_metres * CM_PER_METRE:.0f} cm/s. The CSV "
        f"is generated from the design workbook and is authoritative."
    )


def test_the_default_matches_the_simulation(default_speed_cm) -> None:
    """And the same figure in the model that the tuning is done against."""
    from cataclysm_sim import character

    model_metres = character.DEFAULT_STAT_LINE["movement_speed"].base

    assert default_speed_cm == pytest.approx(model_metres * CM_PER_METRE), (
        f"CataclysmPlayerCharacter.h walks at {default_speed_cm:.0f} cm/s but "
        f"DEFAULT_STAT_LINE['movement_speed'] in sim/cataclysm_sim/character.py "
        f"is {model_metres} metres per second."
    )


def test_the_default_matches_what_the_attribute_starts_at(default_speed_cm) -> None:
    """The constructor's figure and the attribute's must be the same speed.

    THE FAILURE THIS CATCHES. The pawn writes the constant at construction and
    then takes the MovementSpeed attribute as soon as there is an ability system
    to read. If the two disagreed, the player would visibly change speed a
    fraction of a second after spawning, for no reason anybody could see.
    """
    match = re.search(r"InitMovementSpeed\(\s*([0-9.]+)f?\s*\)", source(COMBAT_CPP))
    if match is None:
        pytest.fail(
            "CataclysmCombatAttributeSet.cpp does not call InitMovementSpeed, "
            "so the attribute the pawn follows starts at zero by accident."
        )
    attribute_metres = float(match.group(1))

    assert default_speed_cm == pytest.approx(attribute_metres * CM_PER_METRE), (
        f"The pawn is built walking at {default_speed_cm:.0f} cm/s and then "
        f"reads a MovementSpeed attribute that starts at {attribute_metres} "
        f"metres per second. The player would change speed the moment the "
        f"player state arrives."
    )


def test_the_default_is_a_speed_a_designed_class_actually_uses(
        default_speed_cm) -> None:
    """A stand-in until class selection exists, but one of the real three.

    The issue is explicit that the placeholder should be a designed figure
    rather than another arbitrary one.
    """
    from cataclysm_sim.classes import DEMONIC_CLASSES

    speeds_cm = sorted(
        {round(definition.overrides["movement_speed"].base * CM_PER_METRE, 6)
         if "movement_speed" in definition.overrides
         else round(class_stat("Default", "movement_speed") * CM_PER_METRE, 6)
         for definition in DEMONIC_CLASSES.values()}
    )

    assert default_speed_cm in speeds_cm, (
        f"The player's placeholder speed of {default_speed_cm:.0f} cm/s is not "
        f"one of the three Demonic classes' designed speeds, which are "
        f"{speeds_cm}. There is no class selection yet, so this figure is what "
        f"everybody walks at and it should be a designed one."
    )


def test_the_movement_component_is_actually_given_that_speed() -> None:
    """A constant nothing assigns leaves the engine default in place.

    THE FAILURE MODE THIS EXISTS FOR. Every test above reads a number out of a
    header. All of them would still pass if the line that writes it onto the
    movement component were deleted, and the player would be back at 600 with a
    correct-looking constant sitting beside it.
    """
    text = source(PLAYER_CPP)

    assert re.search(
        r"MaxWalkSpeed\s*=\s*DefaultWalkSpeedCmPerSecond\s*;", text), (
        "CataclysmPlayerCharacter.cpp never assigns DefaultWalkSpeedCmPerSecond "
        "to MaxWalkSpeed. The constant is declared and unused, so the pawn is "
        "built at UCharacterMovementComponent's default of 600 cm/s -- which is "
        "exactly the state issue #391 reported."
    )


def test_the_pawn_follows_the_movement_speed_attribute() -> None:
    """Movement speed has to be a stat, not a constant written once.

    WHY. game/Data/Affixes.csv has an increased movement speed suffix, four boot
    bases in ItemBases.csv carry one as an implicit, Agility scales it in
    Attributes.csv, and EnchantmentsPositive.csv and EnchantmentsNegative.csv
    both move it. If the pawn only ever took a number at construction, every one
    of those would change a stat and nothing about the character.
    """
    text = source(PLAYER_CPP)

    assert "GetMovementSpeedAttribute" in text, (
        "CataclysmPlayerCharacter.cpp does not reference the MovementSpeed "
        "attribute at all, so the pawn's speed is a fixed number again and no "
        "affix, boot, attribute point or enchantment can change how fast the "
        "player moves."
    )

    assert "GetGameplayAttributeValueChangeDelegate" in text, (
        "CataclysmPlayerCharacter.cpp reads the MovementSpeed attribute but "
        "does not subscribe to changes in it, so the player takes a speed once "
        "at spawn and keeps it. Equipping boots would change the stat and not "
        "the character."
    )
