"""Every clip a creature plays goes through `UCataclysmCreatureAnimInstance`, which refuses an additive clip in
single-node mode. Issue #2976.

Single-node play (`USkeletalMeshComponent::PlayAnimation`) of an ADDITIVE clip resets the mesh to its reference pose
and adds the clip to that (UE 5.8 AnimSingleNodeInstanceProxy.cpp lines 331-350), and every Paragon pack's hit
reactions are additive. `UCataclysmCreatureAnimInstance::PlayLoop` and `PlayOneShot` play on the creature's own
instance, into a slot over its loop, and in single-node mode only a clip that is not additive.

THE GUARD: no `PlayAnimation(` call in the game's source outside that helper, except the files listed in
`NOT_YET_MOVED`, each at exactly its listed count, so the list can only shrink. Slice 2 of #2976 moves the other
creatures and empties the creatures from it. Tests are left out: `CataclysmCreatureAnimTests.cpp` calls it on purpose,
as the control that shows the fault.

A comment naming the call does not count, and the scanner proves it on a sample first (the control below).
"""
from __future__ import annotations

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
SOURCE = REPO / "game" / "Source"
HELPER = SOURCE / "Cataclysm" / "Character" / "CataclysmCreatureAnimInstance.cpp"
IMP = SOURCE / "Cataclysm" / "Character" / "CataclysmImpCharacter.cpp"
ENEMY = SOURCE / "Cataclysm" / "Character" / "CataclysmEnemyCharacter.cpp"

#: The helper's two single-node calls: one for loops, one for one-shots.
HELPER_CALLS = 2

#: Files that still call it directly, at exactly these counts, until slice 2 of #2976 moves them. Lower a count when a
#: call moves; never raise one.
NOT_YET_MOVED = {
    "Cataclysm/Character/CataclysmAbyssalWardenCharacter.cpp": 3,
    "Cataclysm/Character/CataclysmCorruptedSentinelCharacter.cpp": 2,
    "Cataclysm/Character/CataclysmGatekeeperCharacter.cpp": 2,
    "Cataclysm/Character/CataclysmHellhoundCharacter.cpp": 2,
    "Cataclysm/Character/CataclysmLichCharacter.cpp": 2,
    "Cataclysm/Character/CataclysmPlayerCharacter.cpp": 1,
    "Cataclysm/Character/CataclysmSuccubusCharacter.cpp": 2,
    "Cataclysm/Character/CataclysmWraithCharacter.cpp": 2,
}

CALL = re.compile(r"\bPlayAnimation\s*\(")


def code(text: str) -> str:
    """Source text with its comments removed, so a comment naming a call cannot pass for the call."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def calls_in(text: str) -> int:
    return len(CALL.findall(code(text)))


def body(text: str, signature: str) -> str:
    """The body of the function whose definition starts with `signature`, braces balanced."""
    start = text.find(signature)
    assert start != -1, f"no {signature!r}"
    opening = text.find("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += {"{": 1, "}": -1}.get(text[index], 0)
        if depth == 0:
            return text[opening:index + 1]
    raise AssertionError(f"{signature!r} is not closed")


def game_sources() -> list[pathlib.Path]:
    return [path for path in SOURCE.rglob("*") if path.suffix in (".cpp", ".h") and "Tests" not in path.parts]


def test_the_scanner_counts_a_call_and_not_a_comment() -> None:
    """THE CONTROL: the count the guard relies on sees a real call and ignores one in a comment."""
    sample = "// Mesh->PlayAnimation(Clip, true);\n/* PlayAnimation(x) */\nMesh->PlayAnimation(Clip, false);\n"
    assert calls_in(sample) == 1


def test_no_clip_is_played_in_single_node_mode_outside_the_helper() -> None:
    found = {}
    for path in game_sources():
        count = calls_in(path.read_text(encoding="utf-8"))
        if count:
            found[path.relative_to(SOURCE).as_posix()] = count
    helper = HELPER.relative_to(SOURCE).as_posix()
    assert found.pop(helper, 0) == HELPER_CALLS, f"{helper} should hold exactly {HELPER_CALLS} single-node calls"
    unexpected = {name: count for name, count in found.items() if name not in NOT_YET_MOVED}
    assert not unexpected, (
        f"a clip is played with PlayAnimation outside UCataclysmCreatureAnimInstance: {unexpected}. Play it through "
        "UCataclysmCreatureAnimInstance::PlayLoop or PlayOneShot, which refuse an additive clip in single-node mode.")
    changed = {name: (found.get(name, 0), count) for name, count in NOT_YET_MOVED.items() if found.get(name, 0) != count}
    assert not changed, (
        f"(found, listed) differ for {changed}: a file listed as not yet moved gained a call, or lost one and its "
        "count was not lowered.")


def test_the_helper_refuses_an_additive_clip_before_each_single_node_call() -> None:
    text = code(HELPER.read_text(encoding="utf-8"))
    for signature, refusal in (
        ("bool UCataclysmCreatureAnimInstance::PlayLoop(", "IsValidAdditive()"),
        ("float UCataclysmCreatureAnimInstance::PlayOneShot(", "RefuseAdditiveOnSingleNode("),
    ):
        function = body(text, signature)
        refused = function.find(refusal)
        played = CALL.search(function)
        assert played, f"{signature} no longer has its single-node call"
        assert 0 <= refused < played.start(), f"{signature} reaches PlayAnimation before {refusal}"


def test_the_imp_plays_over_a_pose_and_the_death_clip_goes_through_the_helper() -> None:
    imp = code(IMP.read_text(encoding="utf-8"))
    dressing = body(imp, "bool ACataclysmImpCharacter::ResolveBody(")
    assert "UCataclysmCreatureAnimInstance::UseOn(MeshComponent)" in dressing, (
        "the Imp no longer puts UCataclysmCreatureAnimInstance on its mesh, so its additive hit reactions would be "
        "refused rather than played")
    enemy = code(ENEMY.read_text(encoding="utf-8"))
    assert re.search(r"UCataclysmCreatureAnimInstance::PlayLoop\(\s*MeshComponent\s*,\s*Clip\s*,", enemy), (
        "the death clip is no longer played through UCataclysmCreatureAnimInstance::PlayLoop")
