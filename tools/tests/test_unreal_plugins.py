"""Which plugins the Unreal project enables, and why each one is there.

WHY THIS EXISTS. Issue #267. Unreal 5.8 ships toolset plugins under
`Engine/Plugins/Experimental/Toolsets`, each exposing a slice of the editor to an
agent through Epic's Model Context Protocol server. `game/Cataclysm.uproject`
enables `AllToolsets`, whose name says it covers them.

IT DOES NOT. `AllToolsets` is an aggregator and its own `.uplugin` names 21
toolsets. The directory holds 26 besides the aggregator, so five sit outside it
and are invisible unless something goes looking. `MetaHumanGenerator` was one of
them, and it is the one that matters: the asset pipeline research on issue #17
established that the hard part of generating a character is that a generated mesh
arrives with no skeleton and no skin weights, and a MetaHuman arrives rigged.

WHAT WAS DONE. `MetaHumanGenerator` added to `game/Cataclysm.uproject`. The other
four were each decided against for a stated reason, recorded in `game/README.md`,
because every enabled plugin costs editor startup time and adds to the surface an
agent has to reason about.

WHAT THESE TESTS CANNOT DO. They read JSON. They cannot start the editor, so they
cannot prove a plugin loads. What they can do is stop the project file and the
README drifting apart, and stop a plugin being removed without the reason for its
removal being written down.

WHAT IS ASSERTED HERE.

    the project file is valid JSON with a plugin list
    the plugins the project depends on to be driven by an agent are all enabled
    MetaHumanGenerator is enabled, and its dependencies exist in the engine when
      the engine is present
    every extra toolset plugin the README lists as enabled really is, and every
      one it lists as not enabled really is not
    the README's count of what AllToolsets covers matches the engine when the
      engine is present
"""

from __future__ import annotations

import json
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
UPROJECT = REPO_ROOT / "game" / "Cataclysm.uproject"
GAME_README = REPO_ROOT / "game" / "README.md"

#: Where Unreal is installed. Absent on continuous integration, which runs on
#: Linux with no engine, so every check that needs it skips rather than fails.
ENGINE = pathlib.Path(r"C:\Program Files\Epic Games\UE_5.8\Engine")
TOOLSETS = ENGINE / "Plugins" / "Experimental" / "Toolsets"

#: Plugins the project cannot be driven by an agent without. Removing any one of
#: them silently removes a capability rather than breaking a build.
REQUIRED = {
    "ModelContextProtocol": "the Model Context Protocol server itself",
    "AllToolsets": "the aggregator covering 21 of the 26 toolsets",
    "PythonScriptPlugin": "runs the generators under tools/ inside the editor",
    "MetaHumanGenerator": "rigged humanoid characters. Issue #267",
}

#: The five toolset plugins `AllToolsets` does not cover, and whether this
#: project enables each. Keep in step with the table in game/README.md.
OUTSIDE_THE_AGGREGATOR = {
    "MetaHumanGenerator": True,
    "ChaosClothAssetToolset": False,
    "LiveCodingToolset": False,
    "MVVMToolset": False,
    "SequencerAnimMixerToolset": False,
}

#: MetaHumanGenerator declares these and they all ship with the engine.
METAHUMAN_DEPENDENCIES = (
    "MetaHumanCharacter", "MetaHumanCoreTech", "MetaHumanSDK",
    "StructUtils", "ToolsetRegistry",
)


@pytest.fixture(scope="module")
def project() -> dict:
    assert UPROJECT.is_file(), f"{UPROJECT} is missing"
    return json.loads(UPROJECT.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def enabled(project: dict) -> set[str]:
    return {p["Name"] for p in project.get("Plugins", []) if p.get("Enabled")}


def find_uplugin(name: str) -> pathlib.Path | None:
    """The plugin's descriptor anywhere under the engine's plugin tree."""
    if not ENGINE.is_dir():
        return None
    return next(ENGINE.joinpath("Plugins").rglob(f"{name}.uplugin"), None)


# --------------------------------------------------------------------------
# The project file
# --------------------------------------------------------------------------

def test_the_project_file_lists_plugins(project):
    assert project.get("Plugins"), (
        "game/Cataclysm.uproject has no Plugins list. Everything below reads "
        "it, so without it they would all pass over nothing.")


@pytest.mark.parametrize(("name", "why"), sorted(REQUIRED.items()))
def test_a_plugin_the_project_depends_on_is_enabled(enabled, name, why):
    assert name in enabled, (
        f"game/Cataclysm.uproject no longer enables {name}, which is {why}. "
        f"Removing it takes a capability away without breaking a build, so if "
        f"that is deliberate, say why in game/README.md and take it out of "
        f"REQUIRED here.")


@pytest.mark.parametrize(("name", "should_be_on"),
                         sorted(OUTSIDE_THE_AGGREGATOR.items()))
def test_each_toolset_outside_the_aggregator_matches_the_readme(
        enabled, name, should_be_on):
    """`AllToolsets` covers 21 of the 26 toolsets. These five are the rest, and
    the README carries a row for each saying whether it is on and why."""
    assert (name in enabled) is should_be_on, (
        f"{name} is {'not ' if should_be_on else ''}enabled in "
        f"game/Cataclysm.uproject, which contradicts the toolset table in "
        f"game/README.md. Change both together. Issue #267.")


def test_the_readme_lists_every_toolset_outside_the_aggregator(enabled):
    """A sixth one appearing in a future engine version, or a name changing,
    should surface here rather than in a table nobody reads."""
    readme = GAME_README.read_text(encoding="utf-8")
    for name in OUTSIDE_THE_AGGREGATOR:
        assert name in readme, (
            f"game/README.md does not mention {name}, which AllToolsets does "
            f"not cover. Its table is the only place a reader learns that "
            f"enabling AllToolsets is not enough. Issue #267.")


# --------------------------------------------------------------------------
# Checks that need the engine, and skip without it
# --------------------------------------------------------------------------

def test_the_aggregator_really_does_not_cover_all_the_toolsets():
    """The claim the whole issue turned on, read from the engine rather than
    remembered. If a future engine version folds these in, this fails and the
    README section can be deleted."""
    if not TOOLSETS.is_dir():
        pytest.skip(f"{TOOLSETS} is not present; no Unreal install")

    descriptor = TOOLSETS / "AllToolsets" / "AllToolsets.uplugin"
    assert descriptor.is_file(), f"{descriptor} is missing"
    covered = {p["Name"] for p in
               json.loads(descriptor.read_text(encoding="utf-8"))["Plugins"]}

    shipped = {d.name for d in TOOLSETS.iterdir()
               if d.is_dir() and (d / f"{d.name}.uplugin").is_file()}
    shipped.discard("AllToolsets")

    assert shipped - covered == set(OUTSIDE_THE_AGGREGATOR), (
        f"the set of toolsets AllToolsets does not cover has changed to "
        f"{sorted(shipped - covered)}. Update OUTSIDE_THE_AGGREGATOR here and "
        f"the table in game/README.md, and decide about any new one.")


@pytest.mark.parametrize("dependency", METAHUMAN_DEPENDENCIES)
def test_every_metahuman_dependency_ships_with_the_engine(dependency):
    """MetaHumanGenerator declares five dependencies. If any were missing,
    enabling it would fail at editor start, which is the failure this project
    cannot see from a test that does not run the editor."""
    if not ENGINE.is_dir():
        pytest.skip(f"{ENGINE} is not present; no Unreal install")
    assert find_uplugin(dependency) is not None, (
        f"MetaHumanGenerator depends on {dependency} and it is not installed "
        f"under {ENGINE / 'Plugins'}. Enabling MetaHumanGenerator will fail at "
        f"editor start.")


def test_the_metahuman_dependency_list_still_matches_the_plugin():
    """Read from the engine, so a new engine version adding a dependency shows
    up here rather than as an editor that will not start."""
    descriptor = find_uplugin("MetaHumanGenerator")
    if descriptor is None:
        pytest.skip("MetaHumanGenerator is not installed; no Unreal install")
    declared = {p["Name"] for p in
                json.loads(descriptor.read_text(encoding="utf-8"))["Plugins"]}
    assert declared == set(METAHUMAN_DEPENDENCIES), (
        f"MetaHumanGenerator now declares {sorted(declared)} as dependencies, "
        f"not {sorted(METAHUMAN_DEPENDENCIES)}. Check the new ones are "
        f"installed before trusting that the editor will start.")
