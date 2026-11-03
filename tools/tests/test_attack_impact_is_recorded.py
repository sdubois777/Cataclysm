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
    every strike an ordinary attack's damage waits on is the measured one,
    the Imp's unmeasured swipes wait for the mean, and the Hellhound's bite
    waits for nothing while it is unmeasured (issue #783)

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
            # THE RATE CELL IS NOT A BARE NUMBER ANY MORE. Two creatures
            # wait before starting their clip, so theirs reads "1.00, after
            # waiting 0.689 s". Requiring a number here dropped both rows
            # and the tests that use them failed reading "no such row".
            r"\|\s*([A-Za-z ]+?)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|"
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
#: The creatures whose attack clip is now started so that its STRIKE meets the
#: moment the damage lands, rather than so that its END does. Issue #784.
#:
#: Each entry is the creature, the header holding its constants, the measured
#: strike moment, the window the clip is fitted to, and the clip's own length.
ALIGNED = [
    ("Gatekeeper", "CataclysmGatekeeperCharacter.h", "Swing1_Medium",
     "CleaveStrikeSeconds", "DreadCleaveWindUpSeconds",
     "CleaveAnimationSeconds", "DesignedAttackIntervalSeconds"),
    ("Succubus", "CataclysmSuccubusCharacter.h", "Primary_Attack_Normal",
     "SoulfireReleaseSeconds", "SoulfireWindUpSeconds",
     "AttackAnimationSeconds", "DesignedAttackIntervalSeconds"),
]


def aligned_play_rate(strike: float, window: float,
                      floor: float, ceiling: float) -> float:
    """`ACataclysmEnemyCharacter::StrikeAlignedPlayRate`, in Python.

    NEVER SLOWER THAN AUTHORED. A clip that reaches its blow sooner than the
    damage lands is DELAYED rather than slowed; stretching one was tried on the
    Brute and read as slow motion."""
    if strike <= 0.0 or window <= 0.0:
        return 1.0
    return min(max(max(1.0, strike / window), floor), ceiling)


def aligned_delay(strike: float, window: float,
                  floor: float, ceiling: float) -> float:
    """`ACataclysmEnemyCharacter::StrikeAlignedDelaySeconds`, in Python."""
    if strike <= 0.0 or window <= 0.0:
        return 0.0
    return max(0.0, window - strike / aligned_play_rate(strike, window,
                                                        floor, ceiling))


@pytest.mark.parametrize(
    "creature,header,clip,strike_name,window_name,length_name,interval_name",
    ALIGNED)
def test_the_measured_strike_reached_the_code(
        creature, header, clip, strike_name, window_name, length_name,
        interval_name):
    """**THE JOIN BETWEEN THE MEASUREMENT AND THE GAME.** The strike moment was
    measured in the editor and written into `game/docs/enemy-source-assets.md`;
    the C++ carries its own copy because it has to compute a delay from it.

    Two copies of one number drift, and in this repository they have. This is
    the one check that keeps them together."""
    recorded = seconds_in(strike_rows().get(clip, ""))
    assert recorded is not None, (
        f"game/docs/enemy-source-assets.md records no single strike time for "
        f"{clip}, and {header} carries {strike_name} computed from one.")

    written = constant(strike_name, header)
    assert written == pytest.approx(recorded, abs=CLOSE_ENOUGH), (
        f"{header} says {strike_name} is {written} and the measurement "
        f"recorded for {clip} is {recorded}. Re-run "
        f"tools/measure_attack_impact.py and change both or neither.")


@pytest.mark.parametrize(
    "creature,header,clip,strike_name,window_name,length_name,interval_name",
    ALIGNED)
def test_the_strike_arrives_exactly_when_the_damage_does(
        creature, header, clip, strike_name, window_name, length_name,
        interval_name):
    """**THE ONE EQUATION THE WHOLE CHANGE IS.** Wait, then play, and the blow
    connects at the moment the damage lands.

    Recomputed from the C++ constants rather than compared against a copy, so a
    change to the wind-up or to the measured strike fails here."""
    strike = constant(strike_name, header)
    window = constant(window_name, header)
    floor = constant("MinimumPlayRate", header)
    ceiling = constant("MaximumPlayRate", header)

    rate = aligned_play_rate(strike, window, floor, ceiling)
    delay = aligned_delay(strike, window, floor, ceiling)

    assert delay + strike / rate == pytest.approx(window, abs=CLOSE_ENOUGH), (
        f"{creature}: waiting {delay:.4f} s and then playing to a strike at "
        f"{strike} s at rate {rate} reaches it at "
        f"{delay + strike / rate:.4f} s, and the damage lands at {window} s.")


@pytest.mark.parametrize(
    "creature,header,clip,strike_name,window_name,length_name,interval_name",
    ALIGNED)
def test_the_whole_clip_still_fits_inside_the_attack_interval(
        creature, header, clip, strike_name, window_name, length_name,
        interval_name):
    """Waiting pushes the recovery later. If it pushed it past the next attack,
    the creature would be cut off mid-swing every time."""
    strike = constant(strike_name, header)
    window = constant(window_name, header)
    length = constant(length_name, header)
    interval = constant(interval_name, header)
    floor = constant("MinimumPlayRate", header)
    ceiling = constant("MaximumPlayRate", header)

    rate = aligned_play_rate(strike, window, floor, ceiling)
    finishes = aligned_delay(strike, window, floor, ceiling) + length / rate

    assert finishes < interval, (
        f"{creature}'s clip finishes {finishes:.4f} s after the wind-up began "
        f"and its attack interval is {interval} s, so the next attack cuts the "
        f"recovery off.")


@pytest.mark.parametrize("creature",
                         ["Gatekeeper", "Succubus", "Brute", "Imp",
                          "Abyssal Warden"])
def test_the_notes_say_these_five_have_no_gap_left(creature):
    """The table is what a reader consults. Fixing the code and leaving the
    table saying the damage is 0.73 seconds late would be worse than either.

    THE LAST THREE JOINED IN ISSUE #783, BY THE OTHER ROUTE. Their ordinary
    attack has no telegraph, so it is the DAMAGE that waits for the clip rather
    than the clip that waits for the damage. Until then this file held a test
    that the table still called those three early."""
    rows = in_play_rows()
    assert creature in rows, (
        f"the 'What that means in play' table has no {creature} row.")

    gap = rows[creature]["gap"].strip().lower()
    assert "none" in gap and "early" not in gap, (
        f"the notes give {creature} a gap of {rows[creature]['gap']!r}. Its "
        f"damage now lands when its clip strikes, so the gap is none. Issues "
        f"#784 and #783.")


# --------------------------------------------------------------------------
# The ordinary attacks whose DAMAGE waits for the clip. Issue #783.
# --------------------------------------------------------------------------

#: (header, the strike constant, the clip it was measured on). Every constant a
#: creature's ordinary attack times its damage from, each pinned to the figure
#: the notes record for its clip. The Imp's B and C swipes and the Hellhound's
#: bite are not here because nothing was measured for them; the two tests after
#: this one hold what is done about each instead.
DELAYED = [
    ("CataclysmBruteCharacter.h", "SwingStrikeSeconds", "Attack_Biped_Melee_A"),
    ("CataclysmImpCharacter.h", "RendAStrikeSeconds", "Attack_A_SetA"),
    ("CataclysmImpCharacter.h", "RendDStrikeSeconds", "Attack_D_SetA"),
    ("CataclysmImpCharacter.h", "RendEStrikeSeconds", "Attack_E_SetA"),
    ("CataclysmAbyssalWardenCharacter.h", "LeftSwingStrikeSeconds",
     "PrimaryAttack_LA_Fast"),
]


@pytest.mark.parametrize("header,strike_name,clip", DELAYED)
def test_the_damage_waits_for_the_measured_strike(header, strike_name, clip):
    """**THE JOIN BETWEEN THE MEASUREMENT AND THE DELAY.** The same check
    `test_the_measured_strike_reached_the_code` makes for the two telegraphed
    creatures, for the constants an ordinary attack's damage now waits on."""
    recorded = seconds_in(strike_rows().get(clip, ""))
    assert recorded is not None, (
        f"game/docs/enemy-source-assets.md records no single strike time for "
        f"{clip}, and {header} times an ordinary attack's damage from "
        f"{strike_name}. Issue #783.")

    written = constant(strike_name, header)
    assert written == pytest.approx(recorded, abs=CLOSE_ENOUGH), (
        f"{header} says {strike_name} is {written} and the measurement "
        f"recorded for {clip} is {recorded}. Re-run "
        f"tools/measure_attack_impact.py and change both or neither.")


def imp_strike_table() -> list[str]:
    """The constants `ACataclysmImpCharacter::RendStrikeSecondsFor` answers, in
    the order it holds them."""
    source = without_comments(
        (CHARACTER_DIR / "CataclysmImpCharacter.cpp").read_text(encoding="utf-8"))
    match = re.search(
        r"static\s+constexpr\s+float\s+StrikeSeconds\s*\[\s*RendAnimationCount"
        r"\s*\]\s*=\s*\{([^}]*)\}", source)
    if match is None:
        pytest.fail(
            "CataclysmImpCharacter.cpp no longer holds RendStrikeSecondsFor's "
            "table as 'static constexpr float StrikeSeconds[RendAnimationCount]"
            " = {...}', so nothing says which swipe waits for which strike.")
    return [name.strip() for name in match.group(1).split(",") if name.strip()]


def imp_swipe_names() -> list[str]:
    source = without_comments(
        (CHARACTER_DIR / "CataclysmImpCharacter.cpp").read_text(encoding="utf-8"))
    match = re.search(r"RendAnimationNames\s*\[\s*RendAnimationCount\s*\]\s*=\s*"
                      r"\{([^}]*)\}", source)
    if match is None:
        pytest.fail("CataclysmImpCharacter.cpp no longer lists "
                    "RendAnimationNames as an initialised array.")
    return re.findall(r'TEXT\("([A-Za-z0-9_]+)"\)', match.group(1))


def test_each_imp_swipe_waits_for_its_own_strike_or_for_the_mean():
    """**THE ORDER IS THE WHOLE OF IT.** `RendStrikeSecondsFor` is indexed by
    the same draw as `RendAnimationNames`, so a table written in a different
    order than the names would give every swipe another swipe's moment and
    every figure would still be right.

    A measured swipe must wait for ITS constant; an unmeasured one -- B and C,
    which the notes record as not measured -- for the mean. Ruled 2026-11-02."""
    names = imp_swipe_names()
    table = imp_strike_table()
    rows = strike_rows()
    assert len(names) == len(table) == 5, (
        f"the Imp has {len(names)} swipe names and {len(table)} strike times; "
        f"RendStrikeSecondsFor needs one for each.")

    for name, entry in zip(names, table, strict=True):
        letter = name.split("_")[1]
        if rows.get(name) == "not measured":
            assert entry == "RendUnmeasuredStrikeSeconds", (
                f"{name} is not measured, and RendStrikeSecondsFor gives it "
                f"{entry} rather than the mean of the measured three.")
        else:
            assert entry == f"Rend{letter}StrikeSeconds", (
                f"{name} is measured, and RendStrikeSecondsFor gives it {entry} "
                f"rather than Rend{letter}StrikeSeconds -- another swipe's "
                f"moment, or the mean.")


def test_the_imps_unmeasured_swipes_wait_for_the_mean_of_the_measured_three():
    """A JUDGEMENT, RULED 2026-11-02, and the derivation is what is held here:
    the mean of exactly the three constants pinned above, written as their mean
    rather than as a number that could drift from them."""
    header = without_comments(
        (CHARACTER_DIR / "CataclysmImpCharacter.h").read_text(encoding="utf-8"))
    match = re.search(
        r"static\s+constexpr\s+float\s+RendUnmeasuredStrikeSeconds\s*=\s*"
        r"\(\s*(\w+)\s*\+\s*(\w+)\s*\+\s*(\w+)\s*\)\s*/\s*3\.0f\s*;", header)
    assert match is not None, (
        "CataclysmImpCharacter.h no longer derives RendUnmeasuredStrikeSeconds "
        "as the mean of three named constants. It was ruled the mean of the "
        "measured swipes; a literal figure would drift from them.")
    assert sorted(match.groups()) == sorted(
        ["RendAStrikeSeconds", "RendDStrikeSeconds", "RendEStrikeSeconds"]), (
        f"RendUnmeasuredStrikeSeconds is the mean of {match.groups()}, not of "
        f"the three measured swipes A, D and E.")

    mean = sum(constant(name, "CataclysmImpCharacter.h")
               for name in match.groups()) / 3.0
    assert mean == pytest.approx(0.276, abs=CLOSE_ENOUGH), (
        f"the mean of the measured swipes is {mean:.4f}, and the notes and "
        f"docs/DECISIONS.md say 0.276 s.")
    assert "0.276 s" in notes(), (
        "game/docs/enemy-source-assets.md no longer says the Imp's unmeasured "
        "swipes land at 0.276 s.")


def test_the_hellhounds_bite_still_lands_at_once_because_it_is_unmeasured():
    """**ZERO, AND ONLY WHILE THE NOTES SAY NOT MEASURED.** Ruled 2026-11-02:
    no figure is invented for a clip that could not be read, so the bite deals
    its damage at once as it did before issue #783. The day the notes record a
    time for it, this fails and says which constant to set."""
    written = constant("MaulStrikeSeconds", "CataclysmHellhoundCharacter.h")
    cell = strike_rows().get("Scorch_Primary_Fire_Med")
    if cell == "not measured":
        assert written == 0.0, (
            f"CataclysmHellhoundCharacter.h gives MaulStrikeSeconds {written}, "
            f"and the notes record Scorch_Primary_Fire_Med as not measured. "
            f"Where did the figure come from?")
    else:
        recorded = seconds_in(cell or "")
        assert recorded is not None and written == pytest.approx(
            recorded, abs=CLOSE_ENOUGH), (
            f"the notes now record {cell!r} for Scorch_Primary_Fire_Med and "
            f"MaulStrikeSeconds is {written}. Set it to the measured figure.")


#: The four ordinary attacks and the strike their AttackTarget times the damage
#: from. Read out of the function body, because a constant pinned to the notes
#: is worth nothing if the code that should use it uses another.
ATTACK_TARGET_STRIKES = [
    ("CataclysmBruteCharacter.cpp", "ACataclysmBruteCharacter",
     "SwingStrikeSeconds"),
    ("CataclysmImpCharacter.cpp", "ACataclysmImpCharacter",
     "RendStrikeSecondsFor(Drawn)"),
    ("CataclysmAbyssalWardenCharacter.cpp", "ACataclysmAbyssalWardenCharacter",
     "LeftSwingStrikeSeconds"),
    ("CataclysmHellhoundCharacter.cpp", "ACataclysmHellhoundCharacter",
     "MaulStrikeSeconds"),
]


@pytest.mark.parametrize("file_name,class_name,strike", ATTACK_TARGET_STRIKES)
def test_attack_target_waits_rather_than_dealing_the_damage_itself(
        file_name, class_name, strike):
    """**THE ONE LINE ISSUE #783 WAS.** Each of the four used to call
    `Super::AttackTarget` -- the damage, at once -- and then start its clip.
    Now each starts its clip and hands the blow to
    `StrikeWhenTheSwingConnects` with its own strike."""
    source = without_comments(
        (CHARACTER_DIR / file_name).read_text(encoding="utf-8"))
    start = source.find(f"void {class_name}::AttackTarget(")
    assert start != -1, f"{file_name} no longer defines {class_name}::AttackTarget"
    body = source[start:source.find("\n}", start)]

    assert "Super::AttackTarget" not in body, (
        f"{class_name}::AttackTarget calls Super::AttackTarget again, which "
        f"deals the damage before the swing connects. Issue #783.")
    assert "StrikeWhenTheSwingConnects(" in body and strike in body, (
        f"{class_name}::AttackTarget does not hand its blow to "
        f"StrikeWhenTheSwingConnects timed from {strike}.")


SOURCE_DIR = REPO_ROOT / "game" / "Source" / "Cataclysm"

#: (file, where the rule's strike is found, what ends that stretch). The two
#: rules that order a creature's ordinary attack at a target wherever it stands.
RULE_STRIKES = [
    ("Dungeon/CataclysmDungeonGameMode.cpp",
     "void ACataclysmDungeonGameMode::StepEchoesOfThePast(", "\n}"),
    ("AbilitySystem/CataclysmSkillTemplates.cpp",
     "if (Params.bCommandStrike", "++Landings;"),
]


@pytest.mark.parametrize("file_name,start_text,end_text", RULE_STRIKES)
def test_a_rules_strike_is_not_measured_against_reach(
        file_name, start_text, end_text):
    """**ECHOES OF THE PAST AND COMPEL STRIKE WHEREVER THE TARGET STANDS.**
    Issue #783 made an ordinary blow wait for its swing and miss a target that
    walked out of reach in the gap. An echo stands at its reach from where the
    player was a second BEFORE it strikes, so through `AttackTarget` a player
    who stood still could dodge it and one who had walked away could not.
    `StrikeAsARuleOrders` is the same swing without the reach test at landing;
    each rule must order its creature's strike through it."""
    source = without_comments(
        (SOURCE_DIR / file_name).read_text(encoding="utf-8"))
    start = source.find(start_text)
    assert start != -1, f"{file_name} no longer holds {start_text!r}"
    body = source[start:source.find(end_text, start)]

    assert "StrikeAsARuleOrders(" in body, (
        f"the rule's strike in {file_name} no longer orders the creature's "
        f"ordinary attack through StrikeAsARuleOrders, so its blow is measured "
        f"against a reach the rule never asked for. Issue #783.")


def test_the_measurement_records_its_negative_control():
    """**AGREEMENT BETWEEN THE THREE RULES IS NOT ENOUGH ON ITS OWN**, and the
    notes have to say so. Three rules that all read hand motion agree with each
    other on any clip where the hand simply moves. The control is a clip on the
    same rig that strikes nothing, and the attack has to beat it.

    Without this the table reads as eight measured clips rather than as eight
    clips that beat a control and five that did not."""
    text = notes()

    assert "The control, and one creature fails it" in notes(), (
        "game/docs/enemy-source-assets.md no longer has the control section. "
        "Every strike figure in it rests on the attack clip beating a clip that "
        "strikes nothing, and without that section the figures read as though "
        "agreement between the rules were sufficient. It is not: "
        "tools/measure_sentinel_release.py established on 2026-08-20 that a "
        "clip which fires nothing can read more strongly than one that does.")

    for control in ("PlantedIntro", "NonCombat_Idle", "Idle_Relaxed"):
        assert control in text, (
            f"the control section no longer names {control}, so the reader "
            f"cannot tell which clip each attack was measured against.")


def test_the_withdrawn_sentinel_figure_is_not_reinstated():
    """1.755 s was published for `Fire_Planted_B` and withdrawn the same day.
    A withdrawn figure that leaves no trace gets published again, so the notes
    keep it named as withdrawn and this refuses it appearing as a measurement."""
    rows = strike_rows()

    assert rows.get("Fire_Planted_B") == "not measured", (
        f"game/docs/enemy-source-assets.md records "
        f"{rows.get('Fire_Planted_B')!r} for Fire_Planted_B. The Corrupted "
        f"Sentinel fails its negative control, so no strike figure from this "
        f"method means anything for it. If a NEW method has measured it, this "
        f"test's reasoning is stale and issue #478 should say how it was done.")

    assert rows.get("Fire_Planted") == "not measured", (
        f"game/docs/enemy-source-assets.md records "
        f"{rows.get('Fire_Planted')!r} for Fire_Planted, which could not be "
        f"read at all: its three rules disagree by 1.04 seconds.")


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
