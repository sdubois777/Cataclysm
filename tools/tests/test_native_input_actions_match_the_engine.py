"""The generator's native input actions and the C++ names have to be the same set.

WHY THIS EXISTS. Issue #731 added a fifth native binding, the key that opens and
closes the carried inventory, and found that nothing on a pull request checked
this join at all.

The chain for a native binding is four links long:

    a name in CataclysmInputActionNames  ->  an entry in NATIVE_ACTIONS
        ->  an Input Action asset the generator writes
        ->  a key mapping in one or both schemes

`ACataclysmPlayerController::SetupInputComponent` looks the action up by the C++
name. If the generator spells it differently, or lists one the C++ does not name,
the lookup answers null and **the key silently does nothing**: there is no error,
no warning, and no failing test outside the editor. That is the same shape of
silent failure `tools/tests/test_controls_table_matches_the_input_assets.py` was
written for, one link further up the chain.

WHAT IS CHECKED. That the set of action names in NATIVE_ACTIONS matches the set
in `CataclysmInputActionNames`, that each C++ constant is spelt the same as the
string it holds, and that every native action is bound to a key in at least one
control scheme.

WHAT IS NOT. That the generator was run, so that the assets on disk match the
script. A `.uasset` is a binary the editor writes and comparing against it needs
the editor. The automation test `Cataclysm.Input.ConfigListsEverySlotAndNativeAction`
is what closes that gap, and it does not run on a pull request either.

HOW THE GENERATOR IS READ. With `ast`, not by importing it: the module imports
`unreal`, which only exists inside the editor.
"""

from __future__ import annotations

import ast
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "tools" / "generate_input_assets.py"
INPUT_CONFIG_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Input"
                       / "CataclysmInputConfig.h")

#: `inline const FName Move = FName(TEXT("Move"));`
CPP_NAME = re.compile(
    r"inline\s+const\s+FName\s+(\w+)\s*=\s*FName\(\s*TEXT\(\s*\"([^\"]+)\"\s*\)\s*\)\s*;")


def generator_list(list_name: str) -> list:
    """One top-level list assignment in the generator, without running it."""
    if not GENERATOR.is_file():
        pytest.skip("tools/generate_input_assets.py is not present")

    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if list_name in [t.id for t in node.targets if isinstance(t, ast.Name)]:
            return ast.literal_eval(node.value)

    raise AssertionError(
        f"tools/generate_input_assets.py has no list called {list_name}. If it "
        f"was renamed, update this test.")


def native_actions_from_the_generator() -> dict[str, str]:
    """Action name in C++ -> the Input Action asset that carries it."""
    # NATIVE_ACTIONS rows are (asset name, action name in C++, value type,
    # display name). The value type is an `unreal.` attribute, which
    # ast.literal_eval cannot evaluate, so the rows are read as source instead.
    tree = ast.parse(GENERATOR.read_text(encoding="utf-8"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if "NATIVE_ACTIONS" not in [
                t.id for t in node.targets if isinstance(t, ast.Name)]:
            continue

        found: dict[str, str] = {}
        for row in node.value.elts:
            asset = ast.literal_eval(row.elts[0])
            action = ast.literal_eval(row.elts[1])
            found[action] = asset
        return found

    raise AssertionError(
        "tools/generate_input_assets.py has no list called NATIVE_ACTIONS.")


def names_from_the_engine() -> dict[str, str]:
    """C++ constant name -> the string it holds, from CataclysmInputActionNames."""
    if not INPUT_CONFIG_HEADER.is_file():
        pytest.fail(
            "game/Source/Cataclysm/Input/CataclysmInputConfig.h does not "
            "exist. If it was renamed, rename it here too.")

    text = INPUT_CONFIG_HEADER.read_text(encoding="utf-8")
    start = text.find("namespace CataclysmInputActionNames")
    assert start != -1, (
        "CataclysmInputConfig.h has no namespace CataclysmInputActionNames. "
        "That namespace is where the controller and the generator agree on how "
        "a native action is spelt.")

    end = text.find("\n}", start)
    assert end != -1, "CataclysmInputActionNames is not closed."

    found = {name: value for name, value in CPP_NAME.findall(text[start:end])}
    assert found, (
        "No 'inline const FName X = FName(TEXT(\"Y\"));' lines were found "
        "inside CataclysmInputActionNames. If the declarations changed shape, "
        "update CPP_NAME in this test.")
    return found


def test_a_cpp_constant_is_spelt_the_same_as_its_string() -> None:
    """`Zoom = FName(TEXT("Zoom"))`, not `FName(TEXT("Zoomm"))`.

    Nothing forces the two to agree, and every other check here compares the
    strings, so a typo inside one would otherwise be invisible on both sides.
    """
    wrong = {name: value for name, value in names_from_the_engine().items()
             if name != value}
    assert not wrong, (
        "These entries in CataclysmInputActionNames hold a string that is not "
        "their own name, which is how a binding goes missing without anything "
        "reading oddly: "
        + ", ".join(f"{name} holds {value!r}" for name, value in wrong.items()))


def test_the_generator_builds_every_action_the_engine_names() -> None:
    engine = set(names_from_the_engine().values())
    built = set(native_actions_from_the_generator())

    missing = engine - built
    assert not missing, (
        "CataclysmInputActionNames in "
        "game/Source/Cataclysm/Input/CataclysmInputConfig.h names these native "
        "actions and NATIVE_ACTIONS in tools/generate_input_assets.py does not "
        "build them, so ACataclysmPlayerController looks each one up and gets "
        "null. The key does nothing and nothing says so: "
        + ", ".join(sorted(missing)))


def test_the_generator_builds_no_action_the_engine_does_not_name() -> None:
    engine = set(names_from_the_engine().values())
    built = set(native_actions_from_the_generator())

    extra = built - engine
    assert not extra, (
        "NATIVE_ACTIONS in tools/generate_input_assets.py builds these native "
        "actions and CataclysmInputActionNames in "
        "game/Source/Cataclysm/Input/CataclysmInputConfig.h names none of "
        "them, so nothing binds them and they are dead assets: "
        + ", ".join(sorted(extra)))


def test_every_native_action_is_on_a_key_somewhere() -> None:
    """An action with no key mapping cannot be triggered by anything.

    IN AT LEAST ONE SCHEME RATHER THAN IN BOTH, because one binding is
    deliberately absent from one of them: under keyboard movement the left mouse
    button is left unbound, so `IA_MoveToCursor` appears only in the mouse
    scheme. The automation test
    `Cataclysm.Input.MappingContextsCoverEveryActionWithoutCollisions` is what
    checks which actions have to be in both.
    """
    assets = set(native_actions_from_the_generator().values())

    mapped = set()
    for list_name in ("MOUSE_MAPPINGS", "KEYBOARD_MAPPINGS"):
        for action_asset, _key, _modifiers in generator_list(list_name):
            mapped.add(action_asset)

    unbound = assets - mapped
    assert not unbound, (
        "These native Input Actions are built by NATIVE_ACTIONS in "
        "tools/generate_input_assets.py and no key in either control scheme "
        "triggers them: " + ", ".join(sorted(unbound)))
