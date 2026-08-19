"""The rarity settings in DefaultGame.ini name real properties on the game mode.

WHY THIS EXISTS. Issue #721 gave `ACataclysmGameMode` three `UPROPERTY(Config)`
rarity settings and a section in `game/Config/DefaultGame.ini` to set them from,
so the project owner can make a creature a Boss and watch loot drop without
recompiling.

**A key in that section that does not match a property name does nothing, and
says nothing.** Unreal reads config keys by name; an unrecognised one is ignored
in silence. So a typo, or a property later renamed in the header, leaves a file
that looks like it configures the game and does not. The operator would set
`BruteRarityStep=4`, see Common drops, and have no way to tell why.

WHAT IS CHECKED BOTH WAYS. Every `Config` property on the class has a key, so a
new setting cannot be added without being documented where it is set; and every
key names a `Config` property, so a stale or misspelled key fails here.

WHAT IS NOT CHECKED. That Unreal actually applies them, which is engine behaviour
rather than this project's, and that the values are sensible. The automation test
`Cataclysm.Sandbox.ACreatureSpawnsAtTheRarityItWasConfiguredWith` is what checks
the spawner writes a rarity onto a creature; continuous integration compiles no
C++, so it does not run on a pull request and this does.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME_MODE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Player"
                    / "CataclysmGameMode.h")
DEFAULT_GAME_INI = REPO_ROOT / "game" / "Config" / "DefaultGame.ini"

#: The section Unreal reads this class's config properties from. It is the
#: module name and the class name, and it has to match exactly.
SECTION = "[/Script/Cataclysm.CataclysmGameMode]"

#: A `UPROPERTY(...)` whose specifiers include `Config`, and the field under it.
#: The inner alternation lets the specifier list contain its own brackets, which
#: it does: `meta = (ClampMin = "0", ClampMax = "5")`.
CONFIG_PROPERTY = re.compile(
    r"UPROPERTY\((?P<body>[^)]*(?:\([^)]*\)[^)]*)*)\)\s*\n"
    r"\s*\w[\w:<>]*\s+(?P<field>\w+)\s*=")

sys.path.insert(0, str(REPO_ROOT / "sim"))


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


@pytest.fixture(scope="module")
def config_fields() -> dict[str, str]:
    """Every `Config` property on the game mode, mapped to its specifiers."""
    found = {}
    for match in CONFIG_PROPERTY.finditer(read(GAME_MODE_HEADER)):
        body = match.group("body")
        if re.search(r"\bConfig\b", body):
            found[match.group("field")] = body

    assert found, (
        "CataclysmGameMode.h has no UPROPERTY carrying the Config specifier. "
        "Issue #721 added three so the rarity a creature spawns at could be set "
        "in game/Config/DefaultGame.ini without recompiling. If they were "
        "removed, remove the ini section too; if the class stopped being "
        "UCLASS(Config = Game), none of them work.")
    return found


@pytest.fixture(scope="module")
def ini_keys() -> dict[str, str]:
    """The keys in the game mode's section of DefaultGame.ini."""
    text = read(DEFAULT_GAME_INI)

    start = text.find(SECTION)
    assert start != -1, (
        f"game/Config/DefaultGame.ini has no {SECTION} section. That section is "
        "where the sandbox rarity settings are meant to be set, and without it "
        "changing one means editing CataclysmGameMode.h and recompiling.")

    after = text[start + len(SECTION):]
    end = after.find("\n[")
    body = after if end == -1 else after[:end]

    keys = {}
    for line in body.splitlines():
        line = line.strip()
        if not line or line.startswith(";") or "=" not in line:
            continue
        name, _, value = line.partition("=")
        keys[name.strip()] = value.strip()
    return keys


def test_the_class_is_configurable_at_all() -> None:
    """`UPROPERTY(Config)` does nothing unless the class says `Config = Game`."""
    header = read(GAME_MODE_HEADER)

    assert re.search(r"UCLASS\([^)]*\bConfig\s*=\s*Game\b[^)]*\)\s*\n"
                     r"class CATACLYSM_API ACataclysmGameMode", header), (
        "ACataclysmGameMode is not UCLASS(Config = Game). Its Config properties "
        "are then read from no file at all, so game/Config/DefaultGame.ini "
        "would be ignored in silence.")


def test_every_ini_key_names_a_real_setting(config_fields, ini_keys) -> None:
    """A key that matches no property is ignored by Unreal without a word."""
    unknown = sorted(set(ini_keys) - set(config_fields))

    assert not unknown, (
        f"game/Config/DefaultGame.ini sets {unknown} under {SECTION}, and "
        f"CataclysmGameMode.h has no Config property by those names. Unreal "
        f"ignores an unrecognised key silently, so those lines do nothing. The "
        f"Config properties are {sorted(config_fields)}.")


def test_every_setting_is_written_down_where_it_is_set(config_fields,
                                                       ini_keys) -> None:
    """A Config property with no key is one nobody knows they can change."""
    missing = sorted(set(config_fields) - set(ini_keys))

    assert not missing, (
        f"CataclysmGameMode.h has Config properties {missing} with no key under "
        f"{SECTION} in game/Config/DefaultGame.ini. The point of marking one "
        f"Config is that it can be changed without recompiling, and an operator "
        f"reading the ini would not know it exists.")


def test_the_rarity_settings_cannot_leave_the_ladder(config_fields) -> None:
    """Each rarity setting is clamped to the model's rarity ladder.

    A step above the ladder matches no row in `game/Data/EnemyDrops.csv`, so the
    creature drops nothing at all; a negative one makes
    `ACataclysmEnemyCharacter::IsBoss` meaningless. The maximum is compared with
    the model rather than with the number 5, so adding a rung fails here instead
    of leaving the new top rung unreachable.
    """
    from cataclysm_sim.enemy_stats import RARITY_ORDER

    top = len(RARITY_ORDER) - 1
    rarity_fields = [name for name in config_fields if name.endswith("RarityStep")]

    assert rarity_fields, (
        "CataclysmGameMode.h has no Config property ending in 'RarityStep'. "
        "Issue #721 added three; if they were renamed, rename them here too.")

    for name in sorted(rarity_fields):
        body = config_fields[name]
        low = re.search(r'ClampMin\s*=\s*"(-?\d+)"', body)
        high = re.search(r'ClampMax\s*=\s*"(-?\d+)"', body)

        assert low and high, (
            f"{name} has no ClampMin and ClampMax. A value typed or configured "
            f"outside the rarity ladder would reach the creature unchanged.")
        assert int(low.group(1)) == 0, (
            f"{name} clamps to a minimum of {low.group(1)}. Common is rung 0 "
            f"and the ladder has nothing below it.")
        assert int(high.group(1)) == top, (
            f"{name} clamps to a maximum of {high.group(1)} and the model's "
            f"ladder in sim/cataclysm_sim/enemy_stats.py ends at {top}, which "
            f"is {RARITY_ORDER[-1]!r}.")


def test_nothing_spawns_as_a_boss_by_default(config_fields, ini_keys) -> None:
    """A play session should not silently become a boss fight.

    Checked in both the header's initialiser and the ini, because either one
    alone decides it: the ini value wins where it is present, and the header's
    is what a build with no config uses.
    """
    header = read(GAME_MODE_HEADER)

    for name in sorted(n for n in config_fields if n.endswith("RarityStep")):
        found = re.search(rf"\bint32\s+{re.escape(name)}\s*=\s*(-?\d+)\s*;",
                          header)
        assert found, f"{name} has no initialiser in CataclysmGameMode.h"
        assert int(found.group(1)) == 0, (
            f"{name} defaults to {found.group(1)} in CataclysmGameMode.h. "
            f"Common is 0, and a default above it makes every play session a "
            f"harder fight than anyone asked for.")

        assert ini_keys.get(name) == "0", (
            f"game/Config/DefaultGame.ini sets {name}={ini_keys.get(name)!r}. "
            f"That file is committed, so a non-zero value here is not one "
            f"person trying something out -- it is the default for everyone. "
            f"Set it back to 0 before committing.")
