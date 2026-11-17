"""Nothing may reach for a Paragon `Sprint_*` clip as a second running gait.

WHY THIS EXISTS. Issue #386. The plan for making the Brute run while chasing was
to give it `Sprint_Biped_Fwd` and keep `Jog_Biped_Fwd` for roaming. The two clips
hold the same bone animation, so swapping between them would change nothing on
screen: the creature would look exactly the same wandering and chasing, and the
requirement that it "uses a running animation rather than the walking one" would
be satisfied in letter and in nothing else.

Confirmed by direct measurement on 2026-08-08 with
`tools/compare_animation_clips.py`, which evaluates both clips and compares the
pelvis and both feet at 25 points through each. Every biped pair and the
quadruped forward pair differ by 0.0000 cm. The figures are in
`game/docs/enemy-source-assets.md`.

WHAT THIS GUARDS, AND WHAT IT CANNOT. Continuous integration has no Paragon packs
-- `game/Content/Paragon*/` is gitignored -- so nothing here can measure an
animation. What it can do is notice a `Sprint_*` clip being wired into the game,
and keep the measurement written down where the next person reaching for one will
find it.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME_SOURCE = REPO_ROOT / "game" / "Source"
ASSET_REFERENCE = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
COMPARE_TOOL = REPO_ROOT / "tools" / "compare_animation_clips.py"

#: The clips that are the same animation as their jog, measured to 0.0000 cm.
#: Forward is the only direction either gait is used in, which is why the three
#: quadruped side and backward pairs are not in this list: they differ by one to
#: two centimetres, which is not a different gait but is not zero either.
CONFIRMED_DUPLICATE_CLIPS = (
    "Sprint_Biped_Fwd",
    "Sprint_Biped_Bwd",
    "Sprint_Biped_Lft",
    "Sprint_Biped_Rt",
    "Sprint_Quad_Fwd",
)


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def cpp_files() -> list[pathlib.Path]:
    if not GAME_SOURCE.is_dir():
        pytest.skip("the Unreal project source is not present")
    return sorted(GAME_SOURCE.rglob("*.h")) + sorted(GAME_SOURCE.rglob("*.cpp"))


def test_no_cpp_source_loads_a_sprint_clip() -> None:
    """The whole point of the measurement, stated as a rule the code must obey.

    THE FAILURE THIS EXISTS FOR is somebody adding a running state, reaching for
    the obviously named clip, and shipping a creature that looks identical
    whether it is wandering or charging. Nothing would error and nothing else
    would notice.
    """
    offenders = []
    for path in cpp_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        for clip in CONFIRMED_DUPLICATE_CLIPS:
            if clip in text:
                offenders.append(f"{path.relative_to(REPO_ROOT)} names {clip}")

    assert not offenders, (
        "C++ in this project loads a Paragon Sprint clip:\n  "
        + "\n  ".join(offenders)
        + "\nEvery one of those holds the same bone animation as the jog beside "
          "it, measured to 0.0000 cm, so selecting between them changes nothing "
          "on screen. See game/docs/enemy-source-assets.md and issue #386."
    )


def test_the_measurement_is_written_down_with_its_figures() -> None:
    """A finding nobody can find again is one somebody will make a third time.

    This has already been measured twice and corroborated a third way, because
    the first method stopped working and the second was only file sizes.
    """
    text = source(ASSET_REFERENCE)

    assert "compare_animation_clips.py" in text, (
        "game/docs/enemy-source-assets.md does not say which tool measured the "
        "Sprint clips. The first method -- "
        "unreal.AnimationBlueprintLibrary.get_bone_pose_for_frame -- is absent "
        "from this engine build, so naming the one that works is what stops the "
        "next person having to find that out again."
    )

    for clip in ("Sprint_Biped_Fwd", "Sprint_Quad_Fwd"):
        assert clip in text, (
            f"game/docs/enemy-source-assets.md no longer mentions {clip}, so the "
            f"reason not to use it is recorded nowhere."
        )

    assert re.search(r"0\.0000 cm", text), (
        "game/docs/enemy-source-assets.md records no measured difference for the "
        "Sprint clips. Without a figure the claim is an assertion, and this "
        "repository has re-measured it twice already."
    )


def test_the_comparison_tool_keeps_a_control_pair() -> None:
    """A run in which everything compares equal proves nothing on its own.

    The tool compares the biped jog against the quadruped jog, which are known to
    be different clips. If that pair were removed, a fault that made every
    comparison return zero would report every pair as the same animation and
    read as a successful confirmation.
    """
    text = source(COMPARE_TOOL)

    assert "THE CONTROL" in text, (
        "tools/compare_animation_clips.py no longer marks a control pair. "
        "Without one, a fault that made every comparison return zero would look "
        "exactly like the finding it is meant to confirm."
    )

    assert '("Jog_Biped_Fwd", "Jog_Quad_Fwd")' in text, (
        "the control pair in tools/compare_animation_clips.py is no longer the "
        "biped jog against the quadruped jog. It has to be two clips known to "
        "differ, and those two differ by 149 cm."
    )


def test_the_tool_refuses_to_call_an_unsampled_pair_the_same() -> None:
    """Zero differences out of zero samples is not agreement.

    A pair whose bones could not be read would otherwise report a largest
    difference of zero and be called the same animation, which is exactly the
    kind of evidence this issue exists to stop being trusted.
    """
    text = source(COMPARE_TOOL)

    assert "CANNOT TELL" in text, (
        "tools/compare_animation_clips.py no longer distinguishes a pair it "
        "could not sample from a pair that matched. Both would then read as "
        "'THE SAME ANIMATION'."
    )

    assert re.search(r"if sampled == 0:", text), (
        "tools/compare_animation_clips.py does not check that anything was "
        "actually sampled before reporting a verdict."
    )
