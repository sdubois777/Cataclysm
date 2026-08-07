"""`game/README.md` describes the Unreal project. This checks it is still true.

WHY THIS FILE EXISTS. The readme had a section headed "What is not here yet"
that claimed the Gameplay Ability System was deliberately left out of the build
dependencies and that `game/Content/` was empty apart from a placeholder. Both
were true when written and both had been false for a long time by the time
anyone noticed (issue #184). The readme is also where the build and asset
regeneration commands live, so it is read often, and it was telling readers the
opposite of what the repository contained.

WHAT IS CHECKED. Two kinds of thing.

1. Claims of absence. The readme says several systems do not exist yet. Each
   check looks in the source tree for the thing said to be missing, and fails if
   it is now there. This is what makes the section decay loudly: the session that
   adds a heads-up display gets a failing test naming the paragraph to delete,
   rather than leaving the claim to rot.

2. File paths. Every path the readme names in backticks must exist. This catches
   a readme that names a script or an asset that was renamed or never existed.

NOTHING HERE ASSERTS A SYSTEM IS ABSENT. A check only fires when the readme
still claims absence. Deleting the paragraph is always a valid way to make one
pass, and is the right one once the system is real.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME_DIR = REPO_ROOT / "game"
README = GAME_DIR / "README.md"
GAME_SOURCE = GAME_DIR / "Source"

#: Directories that exist only after a build or a play session. A path under one
#: of these is legitimately absent from a fresh clone, so it is not checked.
GENERATED_DIRECTORIES = ("Saved", "Binaries", "Intermediate", "Build", "DerivedDataCache")

#: File extensions worth resolving. A backticked token without one of these is
#: prose, a command, a class name or a module name, not a path.
PATH_SUFFIXES = (".py", ".cs", ".md", ".uasset", ".umap", ".uproject", ".xlsx")


def readme_text() -> str:
    return README.read_text(encoding="utf-8")


def source_files() -> list[pathlib.Path]:
    """Every C++ and build file in the Unreal project's source tree."""
    return [
        path
        for pattern in ("*.h", "*.cpp", "*.cs")
        for path in GAME_SOURCE.rglob(pattern)
    ]


def source_contains(pattern: str) -> list[str]:
    """Names of source files matching a regular expression."""
    compiled = re.compile(pattern)
    return sorted(
        path.relative_to(REPO_ROOT).as_posix()
        for path in source_files()
        if compiled.search(path.read_text(encoding="utf-8", errors="replace"))
    )


# --------------------------------------------------------------------------
# The two claims issue #184 was about. These are the reverse of the checks
# below: they fail if the false wording ever comes back.
# --------------------------------------------------------------------------


def test_the_readme_does_not_say_the_ability_system_is_missing() -> None:
    """The Gameplay Ability System is a build dependency and has been for a while."""
    build_rules = (GAME_SOURCE / "Cataclysm" / "Cataclysm.Build.cs").read_text(
        encoding="utf-8"
    )
    if '"GameplayAbilities"' not in build_rules:
        pytest.skip(
            "Cataclysm.Build.cs no longer depends on GameplayAbilities, so the "
            "readme claiming it is absent would be correct."
        )

    text = readme_text()
    assert "Gameplay Ability System is not wired up" not in text, (
        "game/README.md says the Gameplay Ability System is not wired up, but "
        "Cataclysm.Build.cs lists GameplayAbilities as a public dependency and "
        "game/Source/Cataclysm/AbilitySystem/ holds the component, five "
        "attribute sets and the damage calculation. See issue #184."
    )


def test_the_readme_does_not_say_the_content_folder_is_empty() -> None:
    """`game/Content/` holds a map, fourteen data tables and the input assets."""
    assets = sorted(
        path.relative_to(GAME_DIR).as_posix()
        for suffix in ("*.uasset", "*.umap")
        for path in (GAME_DIR / "Content").rglob(suffix)
    )
    if not assets:
        pytest.skip("game/Content/ really is empty, so the readme would be correct.")

    text = readme_text()
    assert "`Content/` is empty" not in text, (
        f"game/README.md says Content/ is empty, but it holds {len(assets)} "
        f"assets, including {assets[0]}. See issue #184."
    )


# --------------------------------------------------------------------------
# Claims of absence. Each fires only while the readme still makes the claim.
# --------------------------------------------------------------------------


def test_no_heads_up_display_claim_is_still_true() -> None:
    """The readme says nothing uses UMG. Fail when something does."""
    if "No heads-up display" not in readme_text():
        pytest.skip("The readme no longer claims there is no heads-up display.")

    users = source_contains(r'"UMG"|UUserWidget|\bAHUD\b')
    assert not users, (
        "game/README.md still says there is no heads-up display and nothing uses "
        f"UMG, but these files do: {', '.join(users)}. Delete that bullet from "
        "the 'What is not here yet' section."
    )


def test_no_save_system_claim_is_still_true() -> None:
    """The readme says there is no USaveGame. Fail when one appears."""
    if "No save or persistence" not in readme_text():
        pytest.skip("The readme no longer claims there is no save system.")

    users = source_contains(r"\bUSaveGame\b")
    assert not users, (
        "game/README.md still says there is no save or persistence, but these "
        f"files reference USaveGame: {', '.join(users)}. Delete that bullet from "
        "the 'What is not here yet' section."
    )


def test_no_empire_runtime_claim_is_still_true() -> None:
    """`CataclysmEmpire` is a module with a build file and nothing in it."""
    if "No empire layer runtime" not in readme_text():
        pytest.skip("The readme no longer claims the empire module is empty.")

    empire = GAME_SOURCE / "CataclysmEmpire"
    boilerplate = {
        "CataclysmEmpire.Build.cs",
        "CataclysmEmpire.cpp",
        "CataclysmEmpire.h",
    }
    real = sorted(
        path.relative_to(REPO_ROOT).as_posix()
        for path in empire.rglob("*")
        if path.is_file() and path.name not in boilerplate
    )
    assert not real, (
        "game/README.md still says the empire layer has no runtime, but "
        f"game/Source/CataclysmEmpire/ now holds: {', '.join(real)}. Delete that "
        "bullet from the 'What is not here yet' section."
    )


def test_the_art_claim_matches_how_many_characters_have_a_mesh() -> None:
    """The readme says exactly one character wears real art.

    THIS BULLET HAD NO GUARD AND THE TWO AROUND IT DID, which is how it stayed
    saying "No art assets of any kind" while the Brute was given the Paragon
    Rampage model. Counting characters that name a content path is a proxy, but
    it is a proxy that moves the moment a second one is dressed.
    """
    text = readme_text()
    if "One character has art" not in text:
        pytest.skip("The readme no longer claims exactly one character has art.")

    character_dir = GAME_DIR / "Source" / "Cataclysm"
    dressed = sorted(
        path.stem
        for path in character_dir.rglob("*.cpp")
        if "Tests" not in path.parts
        and "/Game/Paragon" in path.read_text(encoding="utf-8", errors="replace")
    )

    assert dressed == ["CataclysmBruteCharacter"], (
        "game/README.md says one character has art, but the classes naming a "
        f"Paragon content path are: {', '.join(dressed) or 'none'}. Update the "
        "'What is not here yet' section to match, and update "
        "game/docs/enemy-source-assets.md."
    )


def test_no_procedural_dungeon_claim_is_still_true() -> None:
    """The readme says `L_Sandbox` is the only map this project authored.

    THIRD-PARTY PACKS ARE EXCLUDED, and that is not the test being weakened. The
    six free Paragon character packs each ship demo and lighting maps --
    `Rampage.umap`, `Countess.umap`, `AnimationTestMap.umap` and others, sixteen
    in total. They are downloaded content that `.gitignore` already excludes from
    the repository, and none of them is a level this game plays. Counting them
    would make this guard fail for everyone who has the art installed and pass
    for everyone who does not, which is the opposite of useful.

    The claim being guarded is about the project's own maps, so the folders git
    ignores are skipped here for the same reason git ignores them.
    """
    if "L_Sandbox` is the only map" not in readme_text():
        pytest.skip("The readme no longer claims L_Sandbox is the only map.")

    content = GAME_DIR / "Content"
    maps = sorted(
        path.name
        for path in content.rglob("*.umap")
        if not path.relative_to(content).parts[0].startswith("Paragon")
    )
    assert maps == ["L_Sandbox.umap"], (
        "game/README.md says L_Sandbox is the only map, but game/Content/ holds "
        f"these outside the third-party packs: {', '.join(maps)}."
    )


# --------------------------------------------------------------------------
# Every path the readme names must exist.
# --------------------------------------------------------------------------


def backticked_paths() -> list[str]:
    """Backtick-quoted tokens in the readme that look like repository files."""
    found = []
    for token in re.findall(r"`([^`\n]+)`", readme_text()):
        token = token.strip()
        # Commands, flags and engine paths are not repository files.
        if token.startswith(("-", "/c/", "C:", '"')) or " " in token:
            continue
        # A bare extension, such as `.Build.cs`, names a kind of file rather than
        # one file. The readme uses these when talking about all of them.
        if token.startswith("."):
            continue
        if not token.endswith(PATH_SUFFIXES):
            continue
        if token.split("/")[0] in GENERATED_DIRECTORIES:
            continue
        found.append(token)
    return sorted(set(found))


def test_the_readme_only_names_files_that_exist() -> None:
    """A renamed or invented script name is caught here.

    Paths resolve relative to `game/` first, because the readme lives there and
    its commands are written to be run from there, then relative to the
    repository root, because it also names `sim/` and `tools/`.
    """
    tokens = backticked_paths()
    assert tokens, "Found no file paths in game/README.md at all, which is wrong."

    missing = [
        token
        for token in tokens
        if not (GAME_DIR / token).exists() and not (REPO_ROOT / token).exists()
    ]
    assert not missing, (
        "game/README.md names files that do not exist, relative to either "
        f"game/ or the repository root: {', '.join(missing)}"
    )
