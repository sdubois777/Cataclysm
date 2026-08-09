"""What a projectile looks like is chosen by whoever fires it, never baked in.

WHY THIS EXISTS. Issue #404. Every projectile in the game flew a grey engine
sphere, including the Brute's thrown rock, while the Paragon Rampage pack ships
the rock itself. The mesh could not simply be put into
`ACataclysmProjectile`'s constructor: that class is what all 398 rows of
`game/Data/WeaponSkills.csv` fire through, so a rock there would have armed every
player fire bolt with one.

WHAT CAN GO WRONG, AND WHAT EACH TEST BELOW CATCHES.

    the rock's asset path is renamed or moved   the throw silently flies a sphere
    the Brute stops passing its mesh            the same, and nothing errors
    Fire loses its default of nullptr           every player skill has to be edited
    a mesh is put into the constructor          every fire bolt becomes a rock

WHY IT IS A PYTHON TEST. Continuous integration never builds the C++ and never
opens the editor, and it has no Paragon pack, so the automation tests in
`game/Source/Cataclysm/Tests/CataclysmProjectileBodyTests.cpp` -- which do check
the live actors -- take their no-art path there and prove nothing about the rock.
These read the source text, which is present either way.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
PROJECTILE_H = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                / "CataclysmProjectile.h")
PROJECTILE_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "AbilitySystem"
                  / "CataclysmProjectile.cpp")
BRUTE_CPP = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
             / "CataclysmBruteCharacter.cpp")
ASSET_REFERENCE = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"

#: Where the pack's assets live under the content root, and what the rock is
#: called. Both are checked against the path constant in the C++, because a
#: renamed asset is the failure that produces a sphere with no error anywhere.
PARAGON_CONTENT_ROOT = "/Game/ParagonRampage/"
ROCK_ASSET_NAME = "SM_Rock_To_Hold"


def source(path: pathlib.Path) -> str:
    if not path.is_file():
        pytest.fail(f"{path.relative_to(REPO_ROOT)} does not exist")
    return path.read_text(encoding="utf-8")


def path_constant(text: str, name: str) -> str:
    """The string a `const TCHAR* Class::<name> = TEXT("...")` line holds.

    Written across two lines in the source, so the adjacent string literals are
    joined here the way the compiler joins them.
    """
    match = re.search(
        rf"{re.escape(name)}\s*=\s*((?:\s*TEXT\(\s*)?(?:\s*\"[^\"]*\")+)",
        text)
    if match is None:
        pytest.fail(
            f"no '{name} = TEXT(\"...\")' line was found. If the constant was "
            f"renamed, rename it here too; if it was deleted, nothing records "
            f"where the rock comes from and the throw is back to a sphere."
        )
    return "".join(re.findall(r"\"([^\"]*)\"", match.group(1)))


@pytest.fixture(scope="module")
def rock_path() -> str:
    return path_constant(source(BRUTE_CPP), "ACataclysmBruteCharacter::RockMeshPath")


def test_the_rock_comes_from_the_paragon_pack(rock_path) -> None:
    """It is pack content, so it is absent on a fresh clone by design.

    An asset path that had drifted out of the pack folder would mean either that
    the rock had been copied into this repository -- which would put a binary in
    Git LFS that the licence covers being in the pack, not being redistributed --
    or that it points at nothing.
    """
    assert rock_path.startswith(PARAGON_CONTENT_ROOT), (
        f"ACataclysmBruteCharacter::RockMeshPath is {rock_path!r}, which is not "
        f"under {PARAGON_CONTENT_ROOT}. The Paragon packs are gitignored and "
        f"everything the Brute wears comes from them."
    )


def test_the_rock_path_names_the_rock_asset(rock_path) -> None:
    """An Unreal object path repeats the asset name after the dot.

    Getting that half wrong is the mistake that loads nothing and reports
    nothing: `FSoftObjectPath::TryLoad` simply answers null, the Brute warns
    once, and the throw quietly flies a sphere for ever.
    """
    assert rock_path.endswith(f"/{ROCK_ASSET_NAME}.{ROCK_ASSET_NAME}"), (
        f"ACataclysmBruteCharacter::RockMeshPath is {rock_path!r}. An Unreal "
        f"object path ends '<package>.<object>', so this should end "
        f"'/{ROCK_ASSET_NAME}.{ROCK_ASSET_NAME}'."
    )


def test_the_brute_passes_its_rock_to_the_projectile() -> None:
    """Not a literal, not nullptr, and not left off.

    THE FAILURE THIS EXISTS FOR is the argument being dropped. `Fire`'s mesh
    parameter defaults to nullptr so that every player skill can ignore it, which
    means a Brute that stopped passing one would still compile, still throw, and
    still deal damage -- and fly a sphere again.
    """
    text = source(BRUTE_CPP)

    assert re.search(r"bInBurns\s*=\s*\*/\s*false\s*,\s*RockMesh\s*[,)]", text), (
        "CataclysmBruteCharacter.cpp does not pass RockMesh to "
        "ACataclysmProjectile::Fire. Without it the rock throw flies the "
        "placeholder sphere again, and nothing reports an error because the "
        "parameter is optional."
    )


def test_the_brute_asks_for_a_lob_when_it_throws() -> None:
    """The same failure as the mesh above, in the parameter added after it.

    `Fire`'s flight time parameter defaults to zero so that all 398 player
    skills keep flying straight without knowing it exists. That default is what
    makes losing it silent here: the rock would still be thrown, still be the
    right mesh, still deal its damage, and would go back to travelling flat.

    IT WOULD ALSO BE WORSE THAN THE FLAT THROW EVER WAS, which is why this is
    worth a test of its own rather than left to engine tests that cannot run on
    a pull request. The rock now leaves the creature's hand, well above 250 cm.
    Flat from there it sails over the head of a player whose own is about 192
    cm, at every range. Issues #454 and #459.

    THE PARAMETER WAS AN APEX HEIGHT UNTIL ISSUE #465 and is now a flight time,
    from which the projectile derives both its ground speed and its arc height.
    """
    text = source(BRUTE_CPP)

    assert re.search(r"RockMesh\s*,\s*RockThrowFlightSecondsInUse\s*\(", text), (
        "CataclysmBruteCharacter.cpp does not pass RockThrowFlightSecondsInUse() "
        "to ACataclysmProjectile::Fire, so the thrown rock travels flat. The "
        "parameter is optional and defaults to no lob, so nothing reports an "
        "error. The rock is fired from the hand, so flat means over the "
        "player's head."
    )


def test_the_brute_does_not_also_pass_a_speed() -> None:
    """A lob has no one speed, so passing one would be two answers to one question.

    WHY THIS IS WORTH CHECKING RATHER THAN OBVIOUS. Issue #465. Before it, the
    Brute passed a designed speed of 600 centimetres per second AND an apex, and
    the projectile held the speed along the path constant while tracing the
    shape. Holding the wrong quantity constant is what made the rock cross the
    ground early and then sink slowly onto its marker.

    A ballistic shot is fastest as it lands and slowest at the top of its arc,
    so there is no single figure to state. The flight time is what is designed
    and `ACataclysmProjectile::Fire` works the constant ground speed out from
    it. A speed put back here would be silently ignored, which is worse than
    being wrong: it would read as a tuning knob and do nothing.
    """
    text = source(BRUTE_CPP)

    assert "RockThrowSpeedCmPerSecond" not in text, (
        "CataclysmBruteCharacter.cpp still names RockThrowSpeedCmPerSecond. A "
        "lobbed rock is given a flight time, not a speed; see issue #465. If a "
        "speed is genuinely wanted again, ACataclysmProjectile::Fire has to be "
        "told which of the two governs, because it cannot use both."
    )


def test_the_brute_resolves_the_rock_before_it_needs_it() -> None:
    """Loaded when the body is, not on the frame of the throw.

    A synchronous asset load is a hitch, and the frame an attack lands on is the
    worst one in a fight to take one on.
    """
    text = source(BRUTE_CPP)

    body = re.search(
        r"bool ACataclysmBruteCharacter::ResolveBody\(.*?\n\}", text, re.S)
    if body is None:
        pytest.fail(
            "CataclysmBruteCharacter.cpp has no ResolveBody. If it was renamed, "
            "rename it here; the rock has to be resolved wherever the rest of "
            "the Brute's optional art is."
        )

    assert "RockMeshPath" in body.group(0), (
        "ACataclysmBruteCharacter::ResolveBody does not resolve the rock, so it "
        "is either never loaded or loaded at the moment of the throw."
    )


def test_a_caster_that_passes_nothing_keeps_the_placeholder() -> None:
    """The parameter has to stay optional, or every player skill needs editing.

    `game/Data/WeaponSkills.csv` has 398 rows and every projectile among them
    fires through the same call in CataclysmSkillTemplates.cpp, which passes no
    mesh. If the default were removed, that call would have to name something,
    and the obvious something to name is a mesh -- which is how a fire bolt ends
    up as a rock.
    """
    header = source(PROJECTILE_H)

    assert re.search(r"UStaticMesh\*\s+InBodyMesh\s*=\s*nullptr", header), (
        "ACataclysmProjectile::Fire's mesh parameter no longer defaults to "
        "nullptr. Every caster that does not have its own mesh now has to name "
        "one."
    )


def test_no_mesh_is_baked_into_the_generic_projectile() -> None:
    """The constructor may find engine content and nothing else.

    THIS IS THE RULE THE WHOLE ISSUE TURNS ON. `ACataclysmProjectile` is
    generic. A pack asset in its constructor would give a rock to every
    projectile skill in the game at once, which is exactly why the rock is
    passed in instead.
    """
    text = source(PROJECTILE_CPP)

    found = re.findall(r"TEXT\(\"(/Game/[^\"]*)\"\)", text)
    assert not found, (
        f"CataclysmProjectile.cpp names project or pack content: {found}. This "
        f"class is what all 398 projectile skills fire through, so anything it "
        f"loads itself is worn by every one of them. A caster with its own mesh "
        f"passes it to Fire."
    )


def test_the_asset_reference_no_longer_calls_the_rock_unused() -> None:
    """The document said in terms that none of it was used. Half of it is now.

    `game/docs/enemy-source-assets.md` is where this repository records what the
    Paragon packs contain and what is done with it. Leaving the old sentence
    would send the next reader looking for work that is finished.
    """
    text = source(ASSET_REFERENCE)

    assert "none of it used yet" not in text, (
        "game/docs/enemy-source-assets.md still says the rock and its debris are "
        "'none of it used yet'. The rock is flown by the throw as of issue #404; "
        "the crater is #421 and the fragments are #422."
    )

    assert ROCK_ASSET_NAME in text, (
        f"game/docs/enemy-source-assets.md no longer mentions {ROCK_ASSET_NAME}, "
        f"so what the throw flies is recorded nowhere outside the C++."
    )
