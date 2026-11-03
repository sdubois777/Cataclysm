"""A creature struck by a landed blow flinches away from it, with its own pack's clip.

WHY THIS EXISTS. Issue #745. A creature that was hit showed nothing: it kept
walking, or kept standing, in the pose it was in, and only the floating number
and the impact burst said the blow had landed. All seven Demonic creatures' packs
ship four directional hit-reaction clips, and nothing loaded them.

WHY IT IS CHECKED FROM PYTHON AS WELL. Continuous integration compiles no C++,
so the automation tests in `game/Source/Cataclysm/Tests/CataclysmHitReactionTests.cpp`
never run on a pull request. Reading the source as text does, the arrangement
`tools/tests/test_a_dying_enemy_plays_its_death_animation.py` uses for the death
clips.

WHAT IT HOLDS TOGETHER. The clip names each creature passes to
`SetHitReactionClipsIn`, the table in `game/docs/enemy-source-assets.md`, and --
where the Paragon packs are present -- the files on disk. A path typed wrong
reads as a creature that simply never flinches: `FSoftObjectPath::TryLoad`
answers null, the flinch is chosen and nothing plays, and no error is logged.

AND THE TWO WIRES A READER WOULD NOT SEE GO MISSING: the hook in the damage path
asks the impact burst's question, which refuses a damage-over-time tick, and
every creature's one-shot forgets a flinch it replaces, which is what keeps a
flinch from delaying an attack.

WHAT IS NOT CHECKED HERE. Whether a clip looks right for its side. The automation
command passes `-nullrhi`, so nothing reaches a screen under test.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CHARACTERS = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
VITAL_SET = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
             / "CataclysmVitalAttributeSet.cpp")
CONTENT = REPO_ROOT / "game" / "Content"
ASSET_RECORD = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"

#: The seven, the folder under `game/Content/` their clips live in, and the four
#: names in the order front, back, left, right. Written out here rather than read
#: from either side, so a change to the code or to the document fails rather than
#: moving both together. Listed from the main checkout's files on 2026-11-02.
EXPECTED: dict[str, tuple[str, tuple[str, str, str, str]]] = {
    "Brute": (
        "ParagonRampage/Characters/Heroes/Rampage/Animations",
        ("HitReact_Front", "HitReact_Back", "HitReact_Left", "HitReact_Right")),
    "AbyssalWarden": (
        "ParagonGrux/Characters/Heroes/Grux/Animations",
        ("HitReact_Front", "HitReact_Back", "HitReact_Left", "HitReact_Right")),
    "Imp": (
        "ParagonMinions/Characters/Minions/Down_Minions/Animations/Melee",
        ("HitReact_Front", "HitReact_Back", "HitReact_Left", "HitReact_Right")),
    "CorruptedSentinel": (
        "ParagonMinions/Characters/Minions/Down_Minions/Animations/Siege",
        ("HitReact_Front_Planted", "HitReact_Back_Planted",
         "HitReact_Left_Planted", "HitReact_Right_Planted")),
    "Hellhound": (
        "ParagonIggyScorch/Characters/Heroes/IggyScorch/Animations",
        ("IggyScorch_HitReact_Front", "IggyScorch_HitReact_Back",
         "IggyScorch_HitReact_Left", "IggyScorch_HitReact_Right")),
    "Gatekeeper": (
        "ParagonSevarog/Characters/Heroes/Sevarog/Animations",
        ("Hitreact_Front", "Hitreact_Back", "Hitreact_Left", "Hitreact_Right")),
    "Succubus": (
        "ParagonCountess/Characters/Heroes/Countess/Animations",
        ("Hitreact_Fwd", "Hitreact_Bwd", "Hitreact_Left", "Hitreact_Right")),
}

#: How each creature is named in the asset record's table.
RECORD_NAMES = {
    "Brute": "The Brute",
    "AbyssalWarden": "The Abyssal Warden",
    "Imp": "The Imp",
    "CorruptedSentinel": "The Corrupted Sentinel",
    "Hellhound": "The Hellhound",
    "Gatekeeper": "The Gatekeeper",
    "Succubus": "The Succubus",
}

#: The function each creature's one-shots all pass through. Two creatures play a
#: clip at a stated rate through `PlayOneShotAtRate`, which their `PlayOneShot`
#: calls, so the forgetting belongs there.
ONE_SHOT = {
    "Brute": "PlayOneShot",
    "AbyssalWarden": "PlayOneShot",
    "Imp": "PlayOneShot",
    "CorruptedSentinel": "PlayOneShot",
    "Hellhound": "PlayOneShot",
    "Gatekeeper": "PlayOneShotAtRate",
    "Succubus": "PlayOneShotAtRate",
}

LITERAL = re.compile(r'TEXT\(\s*((?:"[^"]*"\s*)+)\)')


def read(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(
            f"{path.relative_to(REPO_ROOT).as_posix()} does not exist. A struck "
            f"creature's flinch is issue #745; if a file was renamed, rename it "
            f"here too.")
    return path.read_text(encoding="utf-8")


def without_comments(text: str) -> str:
    """The code alone. Every rule here is written down beside the code that
    keeps it, in a comment naming the very call being searched for."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def function_body(text: str, signature: str) -> str:
    """One function's code, comments stripped."""
    start = text.find(signature)
    assert start != -1, f"the source has no {signature!r}"
    end = text.find("\n}", start)
    assert end != -1, f"{signature!r} is not closed"
    return without_comments(text[start:end])


def joined(literal: str) -> str:
    """A wrapped C++ string literal, as the one string the compiler sees."""
    return "".join(re.findall(r'"([^"]*)"', literal))


def cpp_of(creature: str) -> pathlib.Path:
    return CHARACTERS / f"Cataclysm{creature}Character.cpp"


def named_clips(creature: str) -> tuple[str, list[str]]:
    """The folder and the four names a creature passes to `SetHitReactionClipsIn`."""
    text = read(cpp_of(creature))
    code = without_comments(text)
    found = re.search(r"SetHitReactionClipsIn\(\s*(.*?)\)\s*;", code, flags=re.DOTALL)
    assert found, (
        f"Cataclysm{creature}Character.cpp never calls SetHitReactionClipsIn, so "
        f"the creature names no hit-reaction clips and never flinches. Issue #745.")
    arguments = found.group(1)

    folder_argument = arguments.split(",", 1)[0].strip()
    if folder_argument.startswith("TEXT"):
        folder = joined(folder_argument)
        names_text = arguments.split(",", 1)[1]
    else:
        # A NAMED FOLDER, which is the creature's own `AnimationFolder` constant.
        defined = re.search(
            rf"ACataclysm{creature}Character::{re.escape(folder_argument)}\s*=\s*"
            rf"TEXT\(\s*((?:\"[^\"]*\"\s*)+)\)", code)
        assert defined, (
            f"Cataclysm{creature}Character.cpp passes {folder_argument} as the "
            f"folder and does not define it as a literal this test can read.")
        folder = joined(defined.group(1))
        names_text = arguments.split(",", 1)[1]

    names = [joined(literal) for literal in LITERAL.findall(names_text)]
    return folder, names


@pytest.mark.parametrize("creature", sorted(EXPECTED))
def test_each_demonic_creature_names_its_four_clips_in_order(creature: str) -> None:
    folder, names = named_clips(creature)
    expected_folder, expected_names = EXPECTED[creature]

    assert folder == f"/Game/{expected_folder}", (
        f"Cataclysm{creature}Character names its hit-reaction clips in {folder}, "
        f"not /Game/{expected_folder}.")
    assert names == list(expected_names), (
        f"Cataclysm{creature}Character names {names} as its front, back, left and "
        f"right hit reactions, not {list(expected_names)}. The order is "
        f"ECataclysmHitDirection's, so a swap plays the wrong side's clip.")


@pytest.mark.parametrize("creature", sorted(EXPECTED))
def test_each_creature_loads_its_clips_where_it_loads_its_death_clips(creature: str) -> None:
    body = function_body(read(cpp_of(creature)),
                         f"bool ACataclysm{creature}Character::ResolveBody")
    assert "LoadHitReactionClips()" in body, (
        f"Cataclysm{creature}Character::ResolveBody never loads its hit-reaction "
        f"clips, so every flinch is chosen and none plays. Issue #745.")
    assert body.find("LoadHitReactionClips()") > body.find("DeathAnimations"), (
        f"Cataclysm{creature}Character::ResolveBody loads its hit reactions "
        f"before its death clips; they belong in the animation block beside them.")


@pytest.mark.parametrize("creature", sorted(EXPECTED))
def test_each_creatures_one_shot_ends_a_flinch_first(creature: str) -> None:
    """The wire that keeps a flinch from delaying what replaces it.

    A swing, a cast or a roar played over a flinch takes the mesh at once; without
    this the creature would still report itself flinching, and the Abyssal Warden
    would hold its swing for the flinch to end.
    """
    function = ONE_SHOT[creature]
    body = function_body(read(cpp_of(creature)),
                         f"float ACataclysm{creature}Character::{function}(")
    first_statement = body[body.find("{") + 1:].strip().split(";", 1)[0].strip()
    assert first_statement == "ForgetFlinch()", (
        f"Cataclysm{creature}Character::{function} does not forget a flinch as its "
        f"first statement (it begins {first_statement!r}). Whatever it plays "
        f"replaces a flinch on the mesh, and a flinch must never hold back an "
        f"attack. Ruled 2026-11-02, (a).")


@pytest.mark.parametrize("creature", sorted(EXPECTED))
def test_each_creature_plays_its_flinch_through_its_own_one_shot(creature: str) -> None:
    body = function_body(read(cpp_of(creature)),
                         f"float ACataclysm{creature}Character::PlayFlinchClip(")
    assert re.search(r"return\s+PlayOneShot\s*\(\s*Clip\s*\)", body), (
        f"Cataclysm{creature}Character::PlayFlinchClip does not return "
        f"PlayOneShot(Clip). That is what knows whether the creature's mesh is "
        f"driven by a graph slot or in single-node mode, and what records when the "
        f"clip ends so the walk or idle comes back.")


def test_the_warden_cuts_a_flinch_short_when_it_swings() -> None:
    """The one creature whose swing waits for a one-shot still playing."""
    body = function_body(read(cpp_of("AbyssalWarden")),
                         "void ACataclysmAbyssalWardenCharacter::AttackTarget")
    cut = body.find("IsFlinching()")
    waited = body.find("FirstSwingStartsIn")
    assert cut != -1 and waited != -1 and cut < waited, (
        "ACataclysmAbyssalWardenCharacter::AttackTarget does not cut a flinch "
        "short before it works out how long its left swing waits, so a flinch "
        "would delay the swing and its blow. Ruled 2026-11-02, (a).")
    assert re.search(r"OneShotEndsAtSeconds\s*=\s*Now\s*;", body), (
        "ACataclysmAbyssalWardenCharacter::AttackTarget no longer ends the "
        "flinch's hold on the mesh, so the left swing waits for it.")


def test_the_hook_flinches_on_a_landed_hit_and_never_on_a_tick() -> None:
    """Ruled 2026-11-02, (d): only a landed hit flinches."""
    code = without_comments(read(VITAL_SET))
    found = re.search(
        r"if\s*\(\s*UCataclysmImpactEffect::ShouldDrawFor\(\s*Hit\s*,\s*Outcome\s*\)"
        r"\s*&&\s*!bShareOfAnAllysDamage\s*\)\s*\{(.*?)\}\s*\}", code, flags=re.DOTALL)
    assert found, (
        "UCataclysmVitalAttributeSet::PostGameplayEffectExecute no longer guards "
        "the flinch with UCataclysmImpactEffect::ShouldDrawFor, the question that "
        "refuses a damage-over-time tick and a blow that arrived as nothing.")
    assert "FlinchFrom(" in found.group(1), (
        "The landed-hit guard in UCataclysmVitalAttributeSet no longer calls "
        "FlinchFrom, so no creature flinches.")
    assert "WhereTheBlowCameFrom(" in found.group(1), (
        "The flinch is no longer told where the blow came from, so it cannot "
        "choose the clip for that side.")

    impact = function_body(read(REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                                / "CataclysmImpactEffect.cpp"),
                           "bool UCataclysmImpactEffect::ShouldDrawFor")
    assert "bIsDamageOverTime" in impact, (
        "UCataclysmImpactEffect::ShouldDrawFor no longer refuses a "
        "damage-over-time tick, so every burn and poison tick would flinch.")


def test_the_asset_record_lists_each_creatures_four_clips() -> None:
    text = read(ASSET_RECORD)
    assert "## The four clips each creature flinches with" in text, (
        "game/docs/enemy-source-assets.md no longer has the hit-reaction table.")

    for creature, (folder, names) in sorted(EXPECTED.items()):
        row = re.search(
            rf"^\| {re.escape(RECORD_NAMES[creature])} \| `{re.escape(folder)}` \|"
            rf" `{names[0]}` \| `{names[1]}` \| `{names[2]}` \| `{names[3]}` \|\s*$",
            text, flags=re.MULTILINE)
        assert row, (
            f"game/docs/enemy-source-assets.md does not record {RECORD_NAMES[creature]}'s "
            f"hit reactions as {folder}: {', '.join(names)}.")


def test_every_hit_reaction_clip_exists() -> None:
    """A path typed wrong reads as a creature that never flinches.

    SKIPPED WITHOUT THE PACKS, which are gitignored and therefore absent from
    every worktree and from continuous integration. A skip means this check did
    not run, not that it passed.
    """
    checked = 0
    for creature in sorted(EXPECTED):
        folder, names = named_clips(creature)
        on_disk = CONTENT / folder[len("/Game/"):]
        if not on_disk.is_dir():
            continue
        for name in names:
            asset = on_disk / f"{name}.uasset"
            assert asset.is_file(), (
                f"Cataclysm{creature}Character names a hit reaction that does not "
                f"exist at {asset.relative_to(REPO_ROOT).as_posix()}. It would "
                f"load nothing and the creature would never visibly flinch.")
            checked += 1
    if checked == 0:
        pytest.skip("No Paragon pack is present, so no clip could be looked for. "
                    "The packs are gitignored; this runs only in the main checkout.")


def test_the_design_document_records_what_being_hit_looks_like() -> None:
    """A design decision is not real until it is in docs/, per CLAUDE.md."""
    text = " ".join(read(GDD).split())
    for sentence in (
            "A creature struck by a blow flinches away from it.",
            "Only a landed hit flinches.",
            "A flinch never interrupts what the creature is committed to.",
            "One flinch at a time.",
            "A creature that is dead, stunned or knocked down does not flinch."):
        assert sentence in text, (
            f"docs/Cataclysm_GDD_v2.md no longer says {sentence!r}. That is the "
            f"decision issue #745 implements.")
