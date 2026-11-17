"""Every enemy's attack animation must finish before it attacks again.

WHY THIS EXISTS. `game/docs/enemy-source-assets.md` carries a table of each
enemy's attack interval, the shortest attack animation its Paragon model has, the
play rate that clip needs, and a verdict on whether the one fits inside the other.
Nothing checked it, and it had already drifted: the table gave the Brute a 2.8
second interval when `sim/cataclysm_sim/enemy_stats.py` says 1.6. The figure was
2.8 until play testing on 2026-08-07 changed it, and the document was not updated
with it.

That particular drift was harmless -- the Brute's attack is 0.97 seconds and
passes at either figure -- which is exactly why nobody noticed. The next one need
not be.

WHAT "FITS" MEANS, SINCE #369. A clip longer than its interval is not a failure
on its own. The interval is the designed number and the animation is played to
fit it, at `clip length / attack interval`, which is what
`ACataclysmBruteCharacter::PlayOneShot` and `MontageRateFor` already compute. A
clip shorter than its interval plays at its authored speed and the creature waits;
neither of those functions will ever slow a clip down.

So the row fails only when the rate it needs is above `MaximumPlayRate`, the
ceiling in `game/Source/Cataclysm/Character/CataclysmBruteCharacter.h`. That is a
real bound rather than a matter of taste: the engine clamps to it, so a clip
needing more is still longer than its interval after being sped up, and the
creature starts an attack it has not finished.

WHAT THIS CHECKS. That the table's intervals are the model's, that each play rate
is the arithmetic rather than a typed guess, that each verdict follows from the
rate and the ceiling, that the ceiling the document states is the one the C++
actually enforces, that every designed enemy appears, and that a recorded failure
cites an issue so it is tracked rather than tolerated.

WHAT IT DOES NOT CHECK. The animation lengths themselves. Those were read from
the assets through the editor, which continuous integration does not have, so
they are trusted here. Re-measuring them is
`tools/probe_sentinel_animation.py`-shaped work against the Paragon packs.

WIND-UP IS A DIFFERENT AND STRICTER RULE. The design document caps a telegraphed
wind-up at half the attack interval, and the whole-animation length checked here
is not that. The document says plainly that wind-up durations have not been
measured for any enemy; issue #478 is that gap for the Corrupted Sentinel.
"""

from __future__ import annotations

import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ASSET_RECORD = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
BRUTE_HEADER = (REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"
                / "CataclysmBruteCharacter.h")

sys.path.insert(0, str(REPO_ROOT / "sim"))

#: One row of the timing table. The play rate may be emphasised with asterisks,
#: because the one row that is not 1.00 is worth seeing. The verdict is free text
#: so that a failing row can carry its issue number.
ROW = re.compile(
    r"^\|\s*([^|]+?)\s*\|\s*([\d.]+)\s*s\s*\|\s*`([^`]+)`\s*\|"
    r"\s*([\d.]+)\s*s\s*\|\s*\**([\d.]+)\**\s*\|\s*(.+?)\s*\|$",
    re.MULTILINE)

#: The archetype that is not a creature. It has no model and no animation.
NOT_A_CREATURE = "Baseline"

#: How many decimal places the play rate column is written to.
RATE_PLACES = 2


def archetypes() -> dict:
    from cataclysm_sim.enemy_stats import ARCHETYPES

    return ARCHETYPES


def required_play_rate(clip_seconds: float, interval_seconds: float) -> float:
    """What the engine would play a clip of that length at, on that interval.

    The same arithmetic as `ACataclysmBruteCharacter::MontageRateFor` and
    `PlayOneShot`: `max(1, length / window)`. Never below 1, because neither of
    those functions slows a clip down -- a short clip plays at its authored speed
    and the creature waits.

    THE ENGINE'S CLAMP IS DELIBERATELY NOT APPLIED HERE. This returns what the
    clip NEEDS. Comparing that against the ceiling is what decides the verdict,
    and clamping first would return a rate that fits every time and hide the one
    case this file exists to catch.
    """
    if interval_seconds <= 0.0:
        raise ValueError(f"an attack interval of {interval_seconds} is not a "
                         "cycle a clip could be fitted into")
    return max(1.0, clip_seconds / interval_seconds)


def maximum_play_rate() -> float:
    """`MaximumPlayRate` from the Brute's header, which is where it is defined.

    READ RATHER THAN COPIED. A number written down in this file and compared
    against the document would be two copies of a guess agreeing with each other,
    which is the failure CLAUDE.md names: a guard built from the same number it
    checks cannot fail. The C++ constant is what the engine clamps to, so it is
    the only figure that decides anything.

    FAILS RATHER THAN SKIPS when it cannot be found. A skip would take the only
    check on the ceiling with it and still report green.
    """
    if not BRUTE_HEADER.is_file():
        pytest.fail(f"{BRUTE_HEADER.relative_to(REPO_ROOT)} does not exist, so "
                    "the play rate ceiling the engine enforces cannot be read.")

    match = re.search(
        r"static\s+constexpr\s+float\s+MaximumPlayRate\s*=\s*"
        r"(-?\d+(?:\.\d+)?)f\s*;",
        BRUTE_HEADER.read_text(encoding="utf-8"))
    if match is None:
        pytest.fail(
            f"{BRUTE_HEADER.relative_to(REPO_ROOT)} has no "
            "'static constexpr float MaximumPlayRate = <number>f;' line. If it "
            "moved, point this at its new home; if it was deleted, nothing "
            "clamps a montage's play rate any more and this whole file is "
            "checking against a ceiling that no longer exists.")
    return float(match.group(1))


@pytest.fixture(scope="module")
def rows() -> list[tuple[str, float, str, float, float, str]]:
    """The timing table, parsed.

    FAILS RATHER THAN SKIPS when the table is gone. A skip here would take the
    only check on these figures with it and still report green, which is the
    fault issue #406 was closed on.
    """
    if not ASSET_RECORD.is_file():
        pytest.fail(f"{ASSET_RECORD.relative_to(REPO_ROOT)} does not exist")

    text = ASSET_RECORD.read_text(encoding="utf-8")
    section = text.split("## The timing constraint")
    if len(section) < 2:
        pytest.fail(
            "game/docs/enemy-source-assets.md no longer has a 'The timing "
            "constraint' section, so the attack interval each enemy's animation "
            "was chosen against is recorded nowhere.")

    body = section[1].split("\n## ")[0]
    found = [(name, float(interval), clip, float(length), float(rate), verdict)
             for name, interval, clip, length, rate, verdict
             in ROW.findall(body)]

    if not found:
        pytest.fail(
            "the timing table in game/docs/enemy-source-assets.md parsed to no "
            "rows at all, so every test in this file would pass without "
            "checking anything. It gained a play rate column on 2026-08-09; a "
            "row with five columns no longer matches.")
    return found


def test_every_designed_enemy_appears(rows) -> None:
    """A creature missing from the table is a creature nobody checked."""
    listed = {name for name, _, _, _, _, _ in rows}
    expected = {name for name in archetypes() if name != NOT_A_CREATURE}

    missing = sorted(expected - listed)
    assert not missing, (
        f"these enemies have no row in the timing table in "
        f"game/docs/enemy-source-assets.md: {missing}. Each needs an attack "
        f"animation short enough for its attack interval, and an enemy that is "
        f"not in the table is one nobody has checked.")

    unknown = sorted(listed - expected)
    assert not unknown, (
        f"the timing table lists {unknown}, which are not archetypes in "
        f"sim/cataclysm_sim/enemy_stats.py. Either the name is misspelled or "
        f"the creature was removed from the design.")


def test_the_intervals_are_the_ones_the_model_designs(rows) -> None:
    """The drift this file was written for.

    THE FAILURE THIS EXISTS FOR, and it had already happened: somebody retunes
    an attack interval in the model and the document keeps the old figure. The
    document is then advice about a creature that no longer exists, and the next
    person choosing an animation chooses against the wrong number.
    """
    designed = archetypes()

    for name, interval, _, _, _, _ in rows:
        assert name in designed, f"{name} is not a designed archetype"
        assert interval == pytest.approx(designed[name].attack_interval), (
            f"the timing table gives {name} a {interval} s attack interval and "
            f"sim/cataclysm_sim/enemy_stats.py designs {designed[name].attack_interval} s. "
            f"The model is authoritative; update the table, and check the play "
            f"rate and the verdict still hold at the real figure.")


def test_the_brute_clears_the_clip_it_actually_plays() -> None:
    """The table's column is the SHORTEST usable attack, not the one in use.

    WHY THAT GAP MATTERS. Issue #452. The table records `Attack_Melee_A` at
    0.97 s for the Brute, which is what decides whether the interval is
    achievable at all. `ACataclysmBruteCharacter::AttackAnimationPath` names a
    different and longer clip, `Attack_Biped_Melee_A`, measured at 1.0000 s. When
    the project owner settled the interval at 1.2 seconds by playing it on
    2026-08-09, the real margin was a fifth of a second while the table's figures
    suggested 0.23 -- close enough that the difference is worth checking rather
    than assuming.

    NOTHING RATE-SCALES IT, WHICH IS WHY THE #369 RULE DOES NOT RESCUE IT.
    `PlayAttackAnimation` calls `PlayOneShot` with no window, so the clip runs at
    its authored speed rather than being compressed to the interval. An interval
    under the clip length therefore starts a swing the creature has not finished,
    and no play rate is computed to stop it.

    THE LENGTH IS READ OUT OF THE DOCUMENT rather than written here, so there is
    one copy of the measurement and this fails if the document loses it.
    """
    if not ASSET_RECORD.is_file():
        pytest.fail(f"{ASSET_RECORD.relative_to(REPO_ROOT)} does not exist")

    stated = re.search(
        r"`Attack_Biped_Melee_A`,?\s*measured at \*\*([\d.]+) seconds\*\*",
        ASSET_RECORD.read_text(encoding="utf-8"))
    if stated is None:
        pytest.fail(
            "game/docs/enemy-source-assets.md no longer records the measured "
            "length of Attack_Biped_Melee_A, the clip the Brute actually swings "
            "with. Without it nothing knows how short the attack interval may "
            "be. Re-measure it in the editor rather than deleting the sentence.")

    clip_seconds = float(stated.group(1))
    interval = archetypes()["Brute"].attack_interval

    assert clip_seconds <= interval, (
        f"the Brute swings every {interval} s and the clip it plays, "
        f"Attack_Biped_Melee_A, is {clip_seconds} s long. Nothing rate-scales "
        f"it, so the creature starts a swing it has not finished."
    )


def test_each_play_rate_is_the_arithmetic_rather_than_a_typed_guess(
        rows) -> None:
    """Recomputed from the two numbers in the same row.

    THE COLUMN IS DERIVED, so it must never be edited on its own. Changing an
    interval or measuring a clip again changes the rate with it, and a rate left
    behind is a figure somebody would then build a creature against. The
    Sentinel's 1.20 is the only one in the table that is not 1.00.
    """
    for name, interval, clip, length, rate, _ in rows:
        expected = round(required_play_rate(length, interval), RATE_PLACES)

        assert rate == pytest.approx(expected), (
            f"the timing table gives {name} a play rate of {rate} for {clip} at "
            f"{length} s on a {interval} s interval, and max(1, "
            f"{length} / {interval}) is {expected}. The rate is derived from "
            f"the other two columns; recompute it rather than editing it.")


def test_the_document_states_the_ceiling_the_engine_actually_enforces(
) -> None:
    """The stated ceiling against `MaximumPlayRate` in the C++.

    TWO COPIES OF A NUMBER DRIFT. The document explains the ceiling in prose so
    somebody reading it knows why a row can fail, and the engine clamps to the
    constant. If the constant moves and the prose does not, the document explains
    a rule the code no longer applies.
    """
    if not ASSET_RECORD.is_file():
        pytest.fail(f"{ASSET_RECORD.relative_to(REPO_ROOT)} does not exist")

    # NOT `[\d.]+`, WHICH SWALLOWS THE SENTENCE'S FULL STOP and then fails to
    # convert. The number is written "2.50." and the trailing dot is punctuation.
    stated = re.search(r"play rate has a ceiling of \*{0,2}(\d+(?:\.\d+)?)",
                       ASSET_RECORD.read_text(encoding="utf-8"))
    if stated is None:
        pytest.fail(
            "game/docs/enemy-source-assets.md no longer states the play rate "
            "ceiling, so nothing in the document says why a row may fail. The "
            "sentence begins 'The play rate has a ceiling of'.")

    assert float(stated.group(1)) == pytest.approx(maximum_play_rate()), (
        f"game/docs/enemy-source-assets.md states a play rate ceiling of "
        f"{stated.group(1)} and MaximumPlayRate in "
        f"game/Source/Cataclysm/Character/CataclysmBruteCharacter.h is "
        f"{maximum_play_rate()}. The C++ is what the engine clamps to.")


def test_no_clip_needs_a_rate_the_engine_would_clamp(rows) -> None:
    """Above the ceiling, speeding the clip up does not make it fit.

    `MontageRateFor` and `PlayOneShot` both clamp to `MaximumPlayRate`, so a clip
    asking for more than that is played at the ceiling and still overruns its
    interval. The creature then starts an attack it has not finished, which is
    the whole failure this file exists to catch -- and it would be invisible,
    because the code would look as though it had compensated.
    """
    ceiling = maximum_play_rate()

    for name, interval, clip, length, rate, _ in rows:
        assert rate <= ceiling, (
            f"{name} needs {clip} played at {rate} to fit its {interval} s "
            f"attack interval, and MaximumPlayRate clamps at {ceiling}. It "
            f"would be played at {ceiling} and take "
            f"{length / ceiling:.2f} s, overrunning the interval by "
            f"{length / ceiling - interval:.2f} s. Pick a shorter clip, cut "
            f"this one, or lengthen the interval.")


def test_each_verdict_is_the_arithmetic_rather_than_an_opinion(rows) -> None:
    """Recomputed from the row's own numbers and the engine's ceiling.

    NOT A REPEAT OF THE DOCUMENT'S OWN CLAIM. The verdict word is compared
    against what the interval, the length and the ceiling actually give, so a row
    that says Passes while its clip cannot be compressed enough fails here.

    A ROW MAY PASS WHILE ITS CLIP IS LONGER THAN ITS INTERVAL, and one does. The
    Corrupted Sentinel's `Fire_Planted` is 2.40 s against 2.0 s and passes at a
    play rate of 1.20. Before #369 that row was a recorded failure; the rule that
    changed is written at the top of this file.
    """
    ceiling = maximum_play_rate()

    for name, interval, clip, length, rate, verdict in rows:
        fits = rate <= ceiling
        says_passes = verdict.startswith("Passes")

        assert says_passes == fits, (
            f"the timing table says {name} '{verdict}' with {clip} at "
            f"{length} s on a {interval} s interval, needing a play rate of "
            f"{rate} against a ceiling of {ceiling}, which "
            f"{'fits' if fits else 'does not fit'}. The verdict and the numbers "
            f"disagree.")


def test_a_recorded_failure_names_the_issue_tracking_it(rows) -> None:
    """So a known problem is tracked rather than tolerated.

    NO ROW FAILS TODAY. The Corrupted Sentinel's did until #369 settled that its
    firing clip is played at 1.20 to fit, and this check is kept for the next
    one: the pack an enemy is cast from may simply not contain a clip short
    enough, and #369's own options were to cut the animation or to lengthen the
    interval. What must not happen is such a row sitting there as a permanent
    shrug.
    """
    for name, _, _, _, _, verdict in rows:
        if verdict.startswith("Passes"):
            continue
        assert re.search(r"#\d+", verdict), (
            f"{name}'s row records a failure without naming an issue: "
            f"{verdict!r}. A known-broken enemy with nothing tracking it is a "
            f"creature that ships broken.")


def test_the_table_is_in_interval_order(rows) -> None:
    """Because it presents itself that way, and a table sorted by nothing is
    harder to read than one sorted by anything.

    This is the check that catches a row edited in place without being moved,
    which is how the Brute's stale 2.8 s figure sat at the bottom of the table
    looking like the slowest creature in the game.
    """
    intervals = [interval for _, interval, _, _, _, _ in rows]
    assert intervals == sorted(intervals), (
        f"the timing table is no longer in attack interval order: {intervals}. "
        f"Move the row rather than only changing the number.")
