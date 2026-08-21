"""The measured strike moments in game/docs/enemy-source-assets.md, checked.

WHAT THIS IS ABOUT. Issue #526 asked when inside each enemy's ordinary attack
animation the damage actually lands. Nobody knew, so nothing could check that a
creature's telegraph is honest. The measurements are now recorded, and the two
tables that hold them are prose: prose goes stale in silence, which is the whole
reason this file exists.

WHY IT DOES NOT MEASURE ANYTHING ITSELF. The measurement needs the Unreal editor
and takes minutes; continuous integration never opens it. `tools/measure_attack_impact.py`
is what measures. This checks that what was written down is internally consistent
and still describes the clips the C++ actually plays.

WHAT IT CHECKS, and each can fail:

    the notes no longer say the wind-ups have never been measured
    every clip the C++ names as an ordinary attack has a row
    the second table's arithmetic follows from the first table's and the C++
    the play rates are the ones the C++ constants produce

THE ARITHMETIC IS RECOMPUTED RATHER THAN COMPARED TO A COPY, which is the same
shape as `tools/tests/test_enemy_telegraphs.py`. A figure in the table that
somebody edits without redoing the division fails here.
"""

from __future__ import annotations

import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
NOTES = REPO_ROOT / "game" / "docs" / "enemy-source-assets.md"
CHARACTER_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm" / "Character"

#: How close two figures have to be to count as the same. The tables are written
#: to three decimal places and the measurement samples 120 times across a clip,
#: so a thousandth of a second is below what either can resolve.
CLOSE_ENOUGH = 0.005


def notes() -> str:
    if not NOTES.is_file():
        pytest.skip("game/docs/enemy-source-assets.md is not present")
    return NOTES.read_text(encoding="utf-8")


def without_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def constant(name: str, file_name: str) -> float:
    """A `static constexpr float <name> = <number>f;` from one character file."""
    path = CHARACTER_DIR / file_name
    if not path.is_file():
        pytest.fail(f"{file_name} does not exist")
    match = re.search(
        rf"static\s+constexpr\s+float\s+{re.escape(name)}\s*=\s*"
        rf"(-?\d+(?:\.\d+)?)f\s*;",
        without_comments(path.read_text(encoding="utf-8")))
    if match is None:
        pytest.fail(
            f"{file_name} has no 'static constexpr float {name}' line, so the "
            f"figure the notes are checked against has gone.")
    return float(match.group(1))


def strike_rows() -> dict[str, str]:
    """Clip name to the 'Strikes at' cell, from the first table."""
    found = {}
    for match in re.finditer(
            r"\|\s*[A-Za-z ]+\s*\|\s*`([A-Za-z0-9_]+)`\s*\|\s*\*\*([^*]+)\*\*\s*\|",
            notes()):
        found[match.group(1)] = match.group(2).strip()
    return found


def in_play_rows() -> dict[str, dict[str, str]]:
    """Creature to its cells in the 'What that means in play' table."""
    text = notes()
    start = text.find("### What that means in play")
    if start == -1:
        pytest.fail(
            "game/docs/enemy-source-assets.md has no 'What that means in play' "
            "section, so nothing records where the damage lands against where "
            "the animation strikes. Issue #526.")

    found = {}
    for match in re.finditer(
            r"\|\s*([A-Za-z ]+?)\s*\|\s*([\d.]+)\s*\|\s*([^|]+?)\s*\|"
            r"\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|",
            text[start:]):
        creature = match.group(1).strip()
        if creature in ("Creature", "---"):
            continue
        found[creature] = {
            "rate": match.group(2).strip(),
            "strike_in_play": match.group(3).strip(),
            "damage_at": match.group(4).strip(),
            "gap": match.group(5).strip(),
        }
    return found


def seconds_in(cell: str) -> float | None:
    """The first number in a table cell, or None when it states a range."""
    numbers = re.findall(r"(\d+\.\d+)", cell)
    if len(numbers) != 1:
        return None
    return float(numbers[0])


# --------------------------------------------------------------------------
# The claim that used to sit there
# --------------------------------------------------------------------------

def test_the_notes_no_longer_say_the_wind_ups_are_unmeasured():
    """The paragraph this work replaced. It said in as many words that the
    moment damage lands had never been measured, and it was true for a year."""
    assert "have not been measured" not in notes(), (
        "game/docs/enemy-source-assets.md still says the wind-up durations "
        "have not been measured. They were, on 2026-08-21, by "
        "tools/measure_attack_impact.py. Issue #526.")


# --------------------------------------------------------------------------
# Every clip the code plays has a row
# --------------------------------------------------------------------------

#: The clip each creature plays as its ORDINARY attack, and the file that names
#: it. Read out of the character classes rather than out of issue #526, whose
#: table lists the Abyssal Warden's clip as `PrimaryAttack_LA` when the creature
#: plays `PrimaryAttack_LA_Fast`, and lists one clip each for three creatures
#: that alternate between several.
BASIC_ATTACK_CLIPS = {
    "Attack_Biped_Melee_A": "CataclysmBruteCharacter.cpp",
    "Attack_A_SetA": "CataclysmImpCharacter.cpp",
    "Attack_B_SetA": "CataclysmImpCharacter.cpp",
    "Attack_C_SetA": "CataclysmImpCharacter.cpp",
    "Attack_D_SetA": "CataclysmImpCharacter.cpp",
    "Attack_E_SetA": "CataclysmImpCharacter.cpp",
    "Scorch_Primary_Fire_Med": "CataclysmHellhoundCharacter.cpp",
    "PrimaryAttack_LA_Fast": "CataclysmAbyssalWardenCharacter.cpp",
    "PrimaryAttack_RA_Fast": "CataclysmAbyssalWardenCharacter.cpp",
    "Fire_Planted": "CataclysmCorruptedSentinelCharacter.cpp",
    "Fire_Planted_B": "CataclysmCorruptedSentinelCharacter.cpp",
    "Primary_Attack_Normal": "CataclysmSuccubusCharacter.cpp",
    "Swing1_Medium": "CataclysmGatekeeperCharacter.cpp",
}


@pytest.mark.parametrize("clip,file_name", sorted(BASIC_ATTACK_CLIPS.items()))
def test_every_basic_attack_clip_is_still_the_one_the_code_plays(clip, file_name):
    """A clip swapped in the C++ without redoing the measurement would leave the
    notes describing an animation nothing plays."""
    path = CHARACTER_DIR / file_name
    if not path.is_file():
        pytest.fail(f"{file_name} does not exist")

    # TWO SHAPES, BECAUSE THE SEVEN FILES USE BOTH. Five name the clip on
    # its own, `TEXT("Swing1_Medium")`; the Hellhound and the Brute write the
    # whole asset path with the name doubled, `.../Scorch_Primary_Fire_Med.
    # Scorch_Primary_Fire_Med`, split across two string literals so the name
    # is preceded by a slash rather than by a quote. Checking only the first
    # shape failed on the Hellhound.
    source = path.read_text(encoding="utf-8")
    assert f'"{clip}' in source or f"{clip}.{clip}" in source, (
        f"{file_name} no longer names {clip}, and "
        f"game/docs/enemy-source-assets.md records a strike time for it. "
        f"Re-run tools/measure_attack_impact.py for whatever replaced it.")


@pytest.mark.parametrize("clip", sorted(BASIC_ATTACK_CLIPS))
def test_every_basic_attack_clip_has_a_measured_row(clip):
    rows = strike_rows()
    assert clip in rows, (
        f"game/docs/enemy-source-assets.md records no strike time for {clip}, "
        f"which the C++ plays as an ordinary attack. Issue #526 asked for all "
        f"of them.")

    cell = rows[clip]
    assert cell == "not measured" or seconds_in(cell) is not None, (
        f"{clip}'s row says {cell!r}, which is neither a time nor the words "
        f"'not measured'. A row has to say one or the other; a blank is how a "
        f"figure that came from nowhere gets in.")


def test_the_unmeasured_clips_are_named_rather_than_left_out():
    """Four of the thirteen could not be read. Leaving them out of the table
    would read as thirteen measured clips, so they are in it saying so."""
    unmeasured = [clip for clip, cell in strike_rows().items()
                  if cell == "not measured"]
    assert unmeasured, (
        "no clip is recorded as unmeasured. Four were on 2026-08-21 -- two of "
        "the Imp's five, the Hellhound's, and the Corrupted Sentinel's "
        "Fire_Planted. If they have since been measured, this test's claim is "
        "stale; if the rows were deleted, the table now overstates what is "
        "known.")


# --------------------------------------------------------------------------
# The second table's arithmetic follows from the first and from the C++
# --------------------------------------------------------------------------

#: Creature, its clip, the C++ file, the clip-length constant and the window the
#: clip is fitted to. Only the creatures whose row states one figure rather than
#: a range are here; the Imp and the Abyssal Warden alternate several clips and
#: their rows state a band.
FITTED_CLIPS = [
    ("Gatekeeper", "Swing1_Medium", "CataclysmGatekeeperCharacter.h",
     "CleaveAnimationSeconds", "DreadCleaveWindUpSeconds"),
]


@pytest.mark.parametrize("creature,clip,header,length_name,window_name",
                         FITTED_CLIPS)
def test_the_recorded_play_rate_is_the_one_the_code_computes(
        creature, clip, header, length_name, window_name):
    """`PlayOneShot` sets the rate to `max(1, clip length / window)`. The notes
    state that rate, and a rate written by hand goes stale when either constant
    moves."""
    length = constant(length_name, header)
    window = constant(window_name, header)
    computed = max(1.0, length / window)

    rows = in_play_rows()
    assert creature in rows, (
        f"the 'What that means in play' table has no {creature} row.")

    written = float(rows[creature]["rate"])
    assert written == pytest.approx(computed, abs=0.001), (
        f"the notes give {creature} a play rate of {written} and "
        f"{length_name} / {window_name} is {length} / {window} = "
        f"{computed:.4f}.")


@pytest.mark.parametrize("creature,clip", [
    ("Brute", "Attack_Biped_Melee_A"),
    ("Corrupted Sentinel", "Fire_Planted_B"),
    ("Gatekeeper", "Swing1_Medium"),
    ("Succubus", "Primary_Attack_Normal"),
])
def test_the_strike_in_play_is_the_strike_in_the_clip_over_the_play_rate(
        creature, clip):
    """The one piece of arithmetic joining the two tables. A clip played faster
    than authored reaches its strike sooner, in exact proportion."""
    rows = in_play_rows()
    assert creature in rows, (
        f"the 'What that means in play' table has no {creature} row.")

    in_clip = seconds_in(strike_rows().get(clip, ""))
    if in_clip is None:
        pytest.fail(
            f"{clip} has no single measured strike time in the first table, so "
            f"the {creature} row in the second cannot be checked against it.")

    rate = float(rows[creature]["rate"])
    in_play = seconds_in(rows[creature]["strike_in_play"])
    assert in_play is not None, (
        f"the {creature} row states {rows[creature]['strike_in_play']!r} for "
        f"the strike in play, which is not a single time.")

    assert in_play == pytest.approx(in_clip / rate, abs=CLOSE_ENOUGH), (
        f"the notes say {creature} strikes at {in_play} s in play, and "
        f"{in_clip} s in the clip at a play rate of {rate} is "
        f"{in_clip / rate:.4f} s.")


@pytest.mark.parametrize("creature", [
    "Corrupted Sentinel", "Gatekeeper", "Succubus",
])
def test_the_recorded_gap_is_the_distance_between_the_two_moments(creature):
    """The number the two issues rest on. It is the distance between when the
    animation strikes and when the damage lands, and nothing else."""
    rows = in_play_rows()
    assert creature in rows, (
        f"the 'What that means in play' table has no {creature} row.")

    in_play = seconds_in(rows[creature]["strike_in_play"])
    damage_at = seconds_in(rows[creature]["damage_at"])
    gap = seconds_in(rows[creature]["gap"])

    for name, value in (("strike in play", in_play), ("damage lands at",
                                                      damage_at),
                        ("gap", gap)):
        assert value is not None, (
            f"the {creature} row's {name} cell states no single figure: "
            f"{rows[creature]}")

    assert gap == pytest.approx(abs(damage_at - in_play), abs=CLOSE_ENOUGH), (
        f"the notes give {creature} a gap of {gap} s, and the distance between "
        f"a strike at {in_play} s and damage at {damage_at} s is "
        f"{abs(damage_at - in_play):.4f} s.")


def test_the_two_issues_the_measurements_produced_are_named():
    """A measurement that found a defect and did not raise one is a measurement
    nobody acts on."""
    text = notes()
    for number in ("783", "784"):
        assert f"issues/{number}" in text, (
            f"game/docs/enemy-source-assets.md no longer links issue #{number}. "
            f"The strike measurements found two separate defects and each has "
            f"its own issue; without the links the table reads as a record "
            f"rather than as a finding.")
