"""Generated lighting data for a map must not reach a commit.

WHY THIS FILE EXISTS. Unreal writes a map's baked lighting and reflection data
next to the level as `<MapName>_BuiltData.uasset`. Issue #140 records one turning
up untracked at 175,928 bytes after nothing more than opening the editor. It goes
through Git LFS, so each committed copy is stored in full and never shared with
the last one.

THE DECISION IS RECORDED IN `docs/DECISIONS.md`, dated 2026-08-04. In short: this
project renders with Lumen and generates dungeon floors at run time, so it never
bakes lighting and the file is output rather than source. That reasoning is what
makes ignoring it right here; a project that does bake lighting should commit it.

WHAT ALREADY STOPS THE FILE BEING PRODUCED. `tools/generate_input_assets.py`,
which builds the sandbox level, sets both the directional light and the sky light
to Movable. A stationary light above a static floor is what makes the editor
demand a lighting build and then write the file. That landed in pull request #143
while fixing click-to-move, and it is the real reason no built data exists today.
The `.gitignore` rule is the second line of defence. Both are checked here,
because losing either one alone brings the problem back quietly.
"""

from __future__ import annotations

import pathlib
import subprocess

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "tools" / "generate_input_assets.py"

#: Built data paths that must be ignored. The second is a map that does not exist,
#: which is the point: the rule has to cover maps nobody has written yet.
BUILT_DATA_PATHS = (
    "game/Content/Maps/L_Sandbox_BuiltData.uasset",
    "game/Content/Maps/L_SomeFutureDungeon_BuiltData.uasset",
)

#: The two lights the sandbox level generator places. Each must be made Movable,
#: because that is what stops the editor demanding a lighting build.
LIGHT_COMPONENT_VARIABLES = ("sun_component", "sky_light_component")


def is_ignored(path: str) -> bool:
    """Whether git ignores ``path``, which is relative to the repository root.

    ``git check-ignore`` exits 0 when the path is ignored and 1 when it is not.
    Any other exit code is raised rather than read as an answer. Paths here never
    carry a trailing slash: `git check-ignore --no-index "some/dir/"` reports a
    match against an empty pattern in this repository even when no rule matches,
    which was found while writing the guard for issue #195.
    """
    completed = subprocess.run(
        ["git", "check-ignore", "-q", "--no-index", path],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if completed.returncode not in (0, 1):
        raise RuntimeError(
            f"git check-ignore failed on {path!r} with exit code "
            f"{completed.returncode}: {completed.stderr.strip()}"
        )
    return completed.returncode == 0


@pytest.mark.parametrize("path", BUILT_DATA_PATHS)
def test_built_data_for_a_map_is_ignored(path: str) -> None:
    assert is_ignored(path), (
        f"{path} is not ignored, so `git add -A` would commit generated lighting "
        "data into Git LFS. Add *_BuiltData.uasset to .gitignore. See issue #140 "
        "and the 2026-08-04 entry in docs/DECISIONS.md."
    )


def test_the_map_itself_is_still_tracked() -> None:
    """The rule must not swallow the level it sits next to.

    `L_Sandbox.umap` is source and is committed. A pattern broad enough to hide
    it would make the sandbox level vanish from a fresh clone, which is a far
    worse outcome than the churn being fixed.
    """
    assert not is_ignored("game/Content/Maps/L_Sandbox.umap"), (
        "game/Content/Maps/L_Sandbox.umap is ignored. The .gitignore rule for "
        "built lighting data is too broad and is hiding the level itself."
    )


def test_no_built_data_is_tracked_already() -> None:
    """An ignore rule does nothing to a file that is already committed.

    Git ignores untracked paths only. If one of these was committed before the
    rule existed it would keep being versioned, and every editor save would keep
    adding an LFS copy, while `git check-ignore` cheerfully reported it ignored.
    """
    completed = subprocess.run(
        ["git", "ls-files", "--", "*_BuiltData.uasset"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    tracked = [line for line in completed.stdout.splitlines() if line.strip()]
    assert not tracked, (
        "These built data files are tracked by git, so the .gitignore rule has no "
        "effect on them. Remove them with `git rm --cached`:\n  "
        + "\n  ".join(tracked)
    )


@pytest.mark.parametrize("variable", LIGHT_COMPONENT_VARIABLES)
def test_the_sandbox_lights_are_made_movable(variable: str) -> None:
    """The generator must keep both lights Movable.

    This is a check on source text, not on the level asset, and it is worth being
    plain about the difference: it catches the line being deleted or changed, and
    it does not catch a level asset that was edited by hand in the editor and
    saved with a stationary light. The asset-level check would need the editor.

    `ADirectionalLight` defaults to Stationary. A stationary light above the
    static floor is exactly what made the editor demand a lighting build and
    write the built data file in the first place.
    """
    source = GENERATOR.read_text(encoding="utf-8")
    expected = f"{variable}.set_mobility(unreal.ComponentMobility.MOVABLE)"
    assert expected in source, (
        f"tools/generate_input_assets.py no longer contains `{expected}`. A light "
        "that is not Movable makes the editor demand a lighting build and write "
        "<MapName>_BuiltData.uasset next to the map. See issue #140."
    )
