"""What is wired to what inside ABP_Brute, checked against a recorded reading.

WHY THIS EXISTS. Issue #408. `ABP_Brute` shipped with both of its "Blend Poses by
bool" nodes wired the wrong way round, so the Brute played its four-legged chase
animation standing still and its standing animation while moving. Nothing caught
it and nothing could: the two automation tests covering that asset check it is
attached to the character, not what is inside it. An animation Blueprint is a
binary asset, so no pull request can show a diff of it either.

HOW IT IS CHECKED NOW. `tools/read_animation_graph.py` runs inside the editor,
follows each blend node's pose pins to the animation on the other end, and writes
what it found to `game/Data/animation_graph_readings.json` along with the asset's
SHA-256. These tests read that record. They cannot open the asset -- continuous
integration has no editor and no art -- so what they hold is that the record is
current and says the right thing.

THE SAME ARRANGEMENT `game/Data/datatable_asset_sources.json` ALREADY USES for the
generated DataTables: a record of what was built, plus the hash of what it was
built from, plus a test that fails when the source moved.

WHAT THIS CANNOT SEE, AND IT MATTERS. The reader covers the animation graph. It
does not cover the event graph, where `bChasing` and `bMoving` are computed from
the pawn's ground speed. So the 375 cm/s gait threshold that
`test_brute_matches_the_model.py` records is still checked only against a comment.
Issue #430.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
RECORD = REPO_ROOT / "game" / "Data" / "animation_graph_readings.json"
CONTENT = REPO_ROOT / "game" / "Content"
BRUTE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmBruteCharacter.h")

#: The asset this file is about.
BRUTE_GRAPH = "/Game/Enemies/Demonic/Brute/ABP_Brute"

#: What each blend node must carry, by the variable it switches on. The TRUE
#: branch is pin 0, which is the opposite of how the node reads top to bottom --
#: that flipped sense is the whole reason issue #408 exists.
EXPECTED_BLENDS = {
    "bChasing": {"true": "Jog_Quad_Fwd", "false": "Jog_Biped_Fwd"},
    "bMoving": {"true": None, "false": "Idle_Biped"},
}


@pytest.fixture(scope="module")
def record() -> dict:
    if not RECORD.is_file():
        pytest.fail(
            f"{RECORD.relative_to(REPO_ROOT)} does not exist. Regenerate it "
            f"with: python tools/run_editor_python.py tools/read_animation_graph.py"
        )
    return json.loads(RECORD.read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def brute(record) -> dict:
    for graph in record["graphs"]:
        if graph["asset"] == BRUTE_GRAPH:
            return graph
    pytest.fail(
        f"{RECORD.relative_to(REPO_ROOT)} has no reading for {BRUTE_GRAPH}.")


def asset_identity(object_path: str) -> str | None:
    """The SHA-256 of an asset's contents, however the checkout holds it.

    `.uasset` is tracked with git LFS. A checkout that fetched the content holds
    the real bytes; one that did not -- which is every continuous integration
    run, because `actions/checkout` does not fetch LFS unless asked -- holds a
    small text pointer naming the same hash. Both are answered here, so the
    comparison is like for like either way.
    """
    relative = object_path[len("/Game/"):] + ".uasset"
    path = CONTENT / relative
    if not path.is_file():
        return None

    raw = path.read_bytes()
    if raw.startswith(b"version https://git-lfs"):
        found = re.search(rb"oid sha256:([0-9a-f]{64})", raw)
        return found.group(1).decode() if found else None

    return hashlib.sha256(raw).hexdigest()


def blends_by_condition(brute: dict) -> dict[str, dict]:
    """The recorded blend nodes, keyed by the variable each switches on."""
    out = {}
    for blend in brute["blend_by_bool_nodes"]:
        condition = blend.get("condition") or {}
        title = condition.get("variable") or condition.get("title") or ""
        # The title reads "Get bChasing"; the variable is the last word.
        out[title.split()[-1] if title else ""] = blend
    return out


def test_the_recorded_reading_matches_the_asset_on_disk(brute) -> None:
    """A record of an older graph is worse than no record.

    THE FAILURE THIS EXISTS FOR is somebody rewiring the Blueprint and not
    re-running the reader. Every other test in this file would then keep passing
    against a description of the graph as it used to be.
    """
    identity = asset_identity(BRUTE_GRAPH)
    if identity is None:
        pytest.skip(
            f"{BRUTE_GRAPH}.uasset is not in this checkout, so there is nothing "
            f"to compare the record against")

    assert brute["sha256"] == identity, (
        f"game/Data/animation_graph_readings.json describes an ABP_Brute whose "
        f"contents hash to {brute['sha256']}, and the asset in this checkout "
        f"hashes to {identity}. The Blueprint changed and the reading did not. "
        f"Regenerate it with:\n"
        f"  python tools/run_editor_python.py tools/read_animation_graph.py"
    )


def test_the_gait_blend_has_the_running_animation_on_the_true_branch(brute) -> None:
    """The exact fault that shipped, stated as the thing that must be true.

    Both blends were reversed, so the creature played its four-legged chase
    animation standing still and its standing animation while moving.
    """
    blends = blends_by_condition(brute)
    if "bChasing" not in blends:
        pytest.fail(
            f"the recorded reading has no blend switching on bChasing. It has: "
            f"{sorted(blends)}")

    blend = blends["bChasing"]
    true_clip = (blend.get("true_branch") or {}).get("clip")
    false_clip = (blend.get("false_branch") or {}).get("clip")

    assert true_clip == EXPECTED_BLENDS["bChasing"]["true"], (
        f"the TRUE branch of the bChasing blend carries {true_clip!r}. It has to "
        f"carry {EXPECTED_BLENDS['bChasing']['true']!r}, the four-legged chase. "
        f"Remember that TRUE is pin 0, which is the top pin and the opposite of "
        f"how the node reads."
    )
    assert false_clip == EXPECTED_BLENDS["bChasing"]["false"], (
        f"the FALSE branch of the bChasing blend carries {false_clip!r} rather "
        f"than {EXPECTED_BLENDS['bChasing']['false']!r}, the two-legged wander."
    )


def test_the_moving_blend_has_the_idle_on_the_false_branch(brute) -> None:
    """The other half of the same fault."""
    blends = blends_by_condition(brute)
    if "bMoving" not in blends:
        pytest.fail(
            f"the recorded reading has no blend switching on bMoving. It has: "
            f"{sorted(blends)}")

    blend = blends["bMoving"]
    false_clip = (blend.get("false_branch") or {}).get("clip")

    assert false_clip == EXPECTED_BLENDS["bMoving"]["false"], (
        f"the FALSE branch of the bMoving blend carries {false_clip!r} rather "
        f"than {EXPECTED_BLENDS['bMoving']['false']!r}. A Brute that is not "
        f"moving has to be standing."
    )

    # AND ITS TRUE BRANCH IS THE OTHER BLEND, not a clip. That is what makes the
    # two nodes a chain rather than two independent choices.
    true_branch = blend.get("true_branch") or {}
    assert true_branch.get("clip") is None, (
        f"the TRUE branch of the bMoving blend carries a clip directly "
        f"({true_branch.get('clip')!r}). It should carry the gait blend, so that "
        f"a moving Brute then chooses between walking and running."
    )


def test_the_play_rates_are_the_ones_the_header_records(brute) -> None:
    """The figures issue #406 says live in a binary asset with nothing checking them.

    They are checked now, against the comment in CataclysmBruteCharacter.h that
    records them. `test_brute_matches_the_model.py` already pins that comment
    against the designed speeds and the measured authored speeds; this is the
    link that was missing, from the comment to the asset.
    """
    header = BRUTE_HEADER.read_text(encoding="utf-8")

    for clip in ("Jog_Biped_Fwd", "Jog_Quad_Fwd"):
        recorded = next(
            (p["play_rate"] for p in brute["sequence_players"]
             if p["clip"] == clip), None)
        assert recorded is not None, (
            f"the recorded reading has no sequence player for {clip}")

        found = re.search(rf"{clip}\s+([\d.]+)", header)
        if found is None:
            pytest.fail(
                f"CataclysmBruteCharacter.h no longer records a play rate for "
                f"{clip}, so the figure inside the asset is unrecorded again.")

        assert recorded == pytest.approx(float(found.group(1)), abs=0.001), (
            f"ABP_Brute plays {clip} at {recorded} and "
            f"CataclysmBruteCharacter.h says {found.group(1)}. Issue #406 is "
            f"about exactly this pair of numbers being able to drift apart."
        )


def test_the_record_says_what_the_flipped_boolean_sense_is(record) -> None:
    """A reader who does not know it will read the record backwards.

    Which pin index is the true branch is the whole of what went wrong, and it
    is not guessable from the node's appearance.
    """
    note = record.get("the_flipped_boolean_sense", "")
    assert "pin index 0" in note and "FALSE pose on pin 1" in note, (
        "game/Data/animation_graph_readings.json no longer explains that a Blend "
        "Poses by bool node puts the TRUE pose on pin 0. Without that, the "
        "'true_branch' and 'false_branch' entries in it cannot be checked "
        "against the editor by anybody."
    )
