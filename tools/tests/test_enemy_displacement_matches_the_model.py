"""The three enemy abilities that shove the player, checked against the model.

WHY THIS EXISTS. Issue #310 settled that enemies displace the player and named
three abilities that do it. Issue #625 chose the distances and built it. The
distances now live in three places -- `ABILITIES` in
`sim/cataclysm_sim/enemy_abilities.py`, the ability tables in
`docs/Cataclysm_GDD_v2.md`, and a `static constexpr float` on each creature's C++
header -- and the design document copy is already checked by
`tools/tests/test_enemy_abilities.py`. This file checks the third.

IT MATTERS MORE HERE THAN FOR MOST NUMBERS, because continuous integration
compiles no C++ at all. A `static_assert` beside the constant would not run on a
pull request. A Python test that reads the number out of the source as text does.
That is the same arrangement `tools/tests/test_warden_matches_the_model.py` uses
for the Abyssal Warden's charge speed, and its header says why.

ONE OF THE THREE HAS NO C++ AND THAT IS ASSERTED RATHER THAN SKIPPED. The
Hellhound is not built -- only the Brute and the Abyssal Warden have character
classes -- so its Hellrush has nothing to attach a shove to. The number is
decided and recorded in the model so that building the creature does not have to
decide it again. `test_the_hellhound_has_a_distance_and_no_class_to_put_it_on`
below fails as soon as the Hellhound exists, which is when the last third of
issue #625 becomes possible.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CHARACTER_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
BRUTE_HEADER = CHARACTER_DIR / "CataclysmBruteCharacter.h"
WARDEN_HEADER = CHARACTER_DIR / "CataclysmAbyssalWardenCharacter.h"

#: Metres to centimetres. The model states a shove in metres, as every distance
#: in the design does; the engine works in centimetres.
CM_PER_METRE = 100.0


def ability(enemy: str, name: str):
    from cataclysm_sim.enemy_abilities import abilities

    for entry in abilities(enemy):
        if entry.name == name:
            return entry
    pytest.fail(
        f"{enemy} has no ability called {name!r} in ABILITIES in "
        f"sim/cataclysm_sim/enemy_abilities.py. If it was renamed, rename it "
        f"here too; if it was deleted, this guard has nothing left to check.")


def knockback_metres(enemy: str, name: str) -> float:
    entry = ability(enemy, name)
    assert "Knockback" in entry.params, (
        f"{enemy}'s {name} no longer states a Knockback distance. The design "
        f"names it as one of the three abilities that displace the player, so "
        f"without the parameter it shoves nothing.")
    return float(entry.params["Knockback"])


def constant(header: pathlib.Path, name: str) -> float:
    """The value of a `static constexpr float <name> = <number>f;` line."""
    if not header.is_file():
        pytest.fail(f"{header.relative_to(REPO_ROOT)} does not exist")

    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        header.read_text(encoding="utf-8"))
    if match is None:
        pytest.fail(
            f"{header.relative_to(REPO_ROOT)} has no "
            f"'static constexpr float {name} = <number>f;' line. If it was "
            f"renamed, rename it here too; if it was deleted, the shove is "
            f"unguarded and continuous integration compiles no C++ to notice.")
    return float(match.group(1))


# --------------------------------------------------------------------------
# The two that are built
# --------------------------------------------------------------------------

def test_the_brutes_stomp_shoves_what_the_model_says() -> None:
    assert constant(BRUTE_HEADER, "StompKnockbackCm") == pytest.approx(
        knockback_metres("Brute", "Stomp") * CM_PER_METRE), (
        "StompKnockbackCm in game/Source/Cataclysm/Character/"
        "CataclysmBruteCharacter.h has drifted from the Stomp's Knockback in "
        "sim/cataclysm_sim/enemy_abilities.py, which is authoritative.")


def test_the_wardens_stampede_shoves_what_the_model_says() -> None:
    assert constant(WARDEN_HEADER, "StampedeKnockbackCm") == pytest.approx(
        knockback_metres("Abyssal Warden", "Stampede") * CM_PER_METRE), (
        "StampedeKnockbackCm in game/Source/Cataclysm/Character/"
        "CataclysmAbyssalWardenCharacter.h has drifted from Stampede's "
        "Knockback in sim/cataclysm_sim/enemy_abilities.py, which is "
        "authoritative.")


# --------------------------------------------------------------------------
# The rule that decided all three
# --------------------------------------------------------------------------

def test_every_displacing_ability_is_inside_the_band_the_design_names() -> None:
    """3 to 4 metres. The design puts an enemy shove between the player's own two
    numeric knockbacks -- Molten Crush's 3 and Searing Hook's 4 -- and notes that
    Path of Exile's default knockback distance is 4 units.

    A figure outside that band is not wrong on its face, but it stops being the
    thing the design document says it is, and this is where that is noticed.
    """
    for enemy, name in (("Brute", "Stomp"),
                        ("Hellhound", "Hellrush"),
                        ("Abyssal Warden", "Stampede")):
        distance = knockback_metres(enemy, name)
        assert 3.0 <= distance <= 4.0, (
            f"{enemy}'s {name} shoves {distance} metres, outside the 3 to 4 "
            f"metre band the design names in the Stun and Anti-Stun-Lock "
            f"section of docs/Cataclysm_GDD_v2.md.")


def test_an_ability_that_also_stuns_takes_the_smaller_shove() -> None:
    """THE RULE THAT CHOSE THE THREE NUMBERS, and the reason it is a rule rather
    than three separate judgements: being moved and being unable to act at the
    same time is the harshest thing in the vertical slice, so the one ability
    that does both does the smaller of the two.

    Written as a comparison rather than as two literals, so it still means
    something if the band moves.
    """
    stomp = ability("Brute", "Stomp")
    assert "StunSeconds" in stomp.params, (
        "the Brute's Stomp no longer stuns, so the reason it takes the smaller "
        "shove has gone and the three distances need re-deciding.")

    stomp_shove = knockback_metres("Brute", "Stomp")
    for enemy, name in (("Hellhound", "Hellrush"),
                        ("Abyssal Warden", "Stampede")):
        assert "StunSeconds" not in ability(enemy, name).params
        assert knockback_metres(enemy, name) > stomp_shove, (
            f"{enemy}'s {name} does not stun and shoves no further than the "
            f"Brute's Stomp, which does. The rule is that an ability doing both "
            f"takes the low end of the band.")


# --------------------------------------------------------------------------
# The third one, which cannot be built yet
# --------------------------------------------------------------------------

def test_the_hellhound_has_a_distance_and_no_class_to_put_it_on() -> None:
    """The Hellhound is not built. Only the Brute and the Abyssal Warden have
    character classes, so Hellrush has nothing to attach a shove to and the third
    of the three abilities issue #625 names is unimplemented.

    THIS FAILS AS SOON AS THE HELLHOUND EXISTS, which is the point of it. At that
    moment the distance is already decided and recorded, and the remaining work
    is to pass it to the charge exactly as the Abyssal Warden does.
    """
    assert knockback_metres("Hellhound", "Hellrush") == pytest.approx(4.0)

    hellhound_header = CHARACTER_DIR / "CataclysmHellhoundCharacter.h"
    assert not hellhound_header.is_file(), (
        "the Hellhound now has a character class, so the third ability the "
        "design names as displacing the player can finally be built. Give it a "
        "HellrushKnockbackCm of 400 and pass it to BeginCharge the way "
        "ACataclysmAbyssalWardenCharacter does, then replace this test with one "
        "checking that constant against the model.")
