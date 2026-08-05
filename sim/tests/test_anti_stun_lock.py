"""The rule that stops a player being stunned repeatedly with no chance to act.

WHAT THIS IS FOR. Issue #216. The project owner's requirement was that crowd
control must not become tedious the way it is in many games in the genre, where
the smallest hit can stun and a player can be stun-locked until they die.

THE ANSWER, given 2026-08-05, is three rules and not one:

    1. A target with a lot of health is not stunned by small hits at all.
    2. A target that IS stunned cannot be stunned again for at least 5 seconds.
    3. Bosses are immune to stun outright.

BOTH OF THE FIRST TWO ARE NEEDED. A damage threshold alone still allows chain
stunning by large hits. An immunity window alone still allows constant
interruption by small ones.

WHAT IS TESTED HERE AND WHAT IS NOT. Rules 1 and 3 are arithmetic on one hit and
are enforced in `sim/cataclysm_sim/damage.py`, so they are tested. Rule 2 is
about time, and `damage.resolve` has no clock -- it resolves one hit. The
constant `STUN_IMMUNITY_SECONDS` records the figure so the game and the design
document cannot disagree about it, and the tests below check that the constant
says 5 and that the design document says the same. **Nothing here proves the
game enforces the window**, because nothing in this repository implements it yet.
That is stated plainly rather than left to be assumed.

WHY THE THRESHOLD IS 10%. The three surveyed games do not agree -- Last Epoch
uses 5% of maximum health, Path of Exile about 10%, Path of Exile 2 uses 15% --
and 10% is the middle. `damage.py` carries the full reasoning and the sources are
in `docs/DECISIONS.md`.
"""

from __future__ import annotations

import pytest

from cataclysm_sim import damage as dm

#: A defender with no mitigation at all, so the damage that lands is the damage
#: dealt and the threshold is the only thing being measured.
MAX_HEALTH = 10_000.0

#: The design document writes small counts as words, so a test comparing a count
#: read from the skill table against the document's prose has to spell it the
#: same way. Only the range the displacing-skill count could plausibly reach.
NUMBER_WORDS = {
    6: "six", 7: "seven", 8: "eight", 9: "nine", 10: "ten", 11: "eleven",
    12: "twelve", 13: "thirteen", 14: "fourteen", 15: "fifteen",
}


def defender(**kwargs) -> dm.Defender:
    kwargs.setdefault("health", MAX_HEALTH)
    return dm.Defender(**kwargs)


def certain_stun(damage: float, **kwargs) -> dm.Attacker:
    """An attacker that stuns on every eligible hit, so a failure to stun is the
    rule refusing rather than a dice roll."""
    return dm.Attacker(damage=damage, subtype="Blunt",
                       bonus_stun_chance=100.0, **kwargs)


def landed(attacker: dm.Attacker, target: dm.Defender) -> dm.Resolution:
    return dm.resolve(attacker, target, force_evade=False, force_block=False)


# --------------------------------------------------------------------------
# Rule 1: a small hit cannot stun a healthy target
# --------------------------------------------------------------------------

def test_the_threshold_is_a_share_of_maximum_health_not_a_flat_number():
    """A flat threshold would be trivial at high health and impossible at low,
    which is the opposite of the requirement."""
    assert dm.STUN_DAMAGE_THRESHOLD == 10.0
    small = defender(health=100.0)
    large = defender(health=100_000.0)
    assert dm.can_be_stunned(10.0, small)
    assert not dm.can_be_stunned(10.0, large)


@pytest.mark.parametrize("share,expected", [
    (0.0, False),
    (0.05, False),
    (0.099, False),
    (0.10, True),
    (0.50, True),
    (1.00, True),
])
def test_a_hit_stuns_only_once_it_takes_enough_of_the_health_bar(share,
                                                                expected):
    assert dm.can_be_stunned(MAX_HEALTH * share, defender()) is expected


def test_a_small_hit_does_not_stun_however_certain_the_attacker_is():
    """The requirement in one test. 100% chance to stun, and it still does not,
    because the hit was a scratch."""
    scratch = certain_stun(MAX_HEALTH * 0.05)
    result = landed(scratch, defender())
    assert result.dealt_to_health == pytest.approx(MAX_HEALTH * 0.05)
    assert not result.stunned
    assert result.stun_seconds == 0.0


def test_a_heavy_hit_from_the_same_attacker_does_stun():
    """The other half, or the test above would pass on an attacker that could
    never stun anything."""
    heavy = certain_stun(MAX_HEALTH * 0.20)
    assert landed(heavy, defender()).stunned


def test_the_threshold_reads_damage_dealt_rather_than_damage_swung():
    """A well defended character stops being interrupted by chip damage, which
    is the point of the rule. Two identical hits, one against a defender whose
    armour takes it below the threshold."""
    swung = certain_stun(MAX_HEALTH * 0.12)
    assert landed(swung, defender()).stunned

    armoured = defender(damage_reduction=50.0)
    result = landed(swung, armoured)
    assert result.dealt_to_health < MAX_HEALTH * dm.STUN_DAMAGE_THRESHOLD / 100
    assert not result.stunned


def test_a_defender_with_no_health_is_not_stunnable_rather_than_always():
    """Zero maximum health would make the threshold zero, so every hit would
    clear it. Refused instead."""
    assert not dm.can_be_stunned(1.0, defender(health=0.0))


# --------------------------------------------------------------------------
# A skill whose stated purpose is to stun ignores the threshold
# --------------------------------------------------------------------------

def test_a_designed_stun_ignores_the_damage_threshold():
    """Four shipped skills in `game/Data/WeaponSkills.csv` stun by design --
    Shield Bash, Shockwave Leap, Lunge and Whip Swing. A threshold that made
    Shield Bash fail against a healthy target would leave the skill doing
    nothing it was written to do."""
    tiny = dm.Attacker(damage=MAX_HEALTH * 0.01, subtype="Blunt",
                       bonus_stun_chance=100.0, stun_is_designed=True)
    assert not dm.can_be_stunned(MAX_HEALTH * 0.01, defender())
    assert landed(tiny, defender()).stunned


def test_a_designed_stun_still_obeys_crowd_control_resistance():
    """It skips the threshold, not every defence. A character at 100 crowd
    control resistance cannot be stunned at all, which the design document has
    said since before this rule existed."""
    tiny = dm.Attacker(damage=MAX_HEALTH * 0.01, subtype="Blunt",
                       bonus_stun_chance=100.0, stun_is_designed=True)
    immune = defender(crowd_control_resistance=100.0)
    assert dm.effective_stun_chance(tiny, immune) == pytest.approx(0.0)
    assert not landed(tiny, immune).stunned


def test_an_ordinary_attack_is_not_a_designed_stun_by_default():
    """The flag has to be opted into, or every Blunt swing would bypass the
    threshold and the rule would do nothing."""
    assert dm.Attacker(damage=1.0).stun_is_designed is False


# --------------------------------------------------------------------------
# Rule 3: a boss cannot be stunned
# --------------------------------------------------------------------------

def test_a_boss_cannot_be_stunned_by_a_heavy_hit():
    huge = certain_stun(MAX_HEALTH * 0.90)
    assert landed(huge, defender()).stunned
    assert not landed(huge, defender(is_boss=True)).stunned


def test_a_boss_cannot_be_stunned_by_a_designed_stun_either():
    """This is the half of the rule that the issue said had to be decided
    together: whatever stops the player being stun-locked is what stops the
    player chain-stunning a boss. A designed stun skips the threshold and does
    not skip this."""
    bash = dm.Attacker(damage=MAX_HEALTH * 0.90, subtype="Blunt",
                       bonus_stun_chance=100.0, stun_is_designed=True)
    assert landed(bash, defender()).stunned
    assert not landed(bash, defender(is_boss=True)).stunned


def test_boss_immunity_is_checked_before_the_threshold():
    assert not dm.can_be_stunned(MAX_HEALTH, defender(is_boss=True))


def test_a_defender_is_not_a_boss_by_default():
    assert dm.Defender(health=1.0).is_boss is False


# --------------------------------------------------------------------------
# Rule 2: the immunity window. Recorded, not enforced here.
# --------------------------------------------------------------------------

def test_the_immunity_window_is_five_seconds():
    """The project owner said "at least a 5 second stun immunity window".

    NOT ENFORCED IN THIS MODULE. `damage.resolve` resolves one hit and has no
    clock. This pins the number so the design document, the model and whatever
    the game eventually implements cannot disagree about it.
    """
    assert dm.STUN_IMMUNITY_SECONDS == 5.0


def test_the_window_is_longer_than_any_stun_the_game_can_apply():
    """Otherwise a target could be stunned again before recovering, which is the
    thing the window exists to prevent. The longest stun in
    `game/Data/WeaponSkills.csv` and the enchantment tables is 3 seconds, from
    the Brute's Heart set bonus."""
    longest_designed_stun = 3.0
    assert dm.STUN_IMMUNITY_SECONDS > longest_designed_stun
    assert dm.STUN_IMMUNITY_SECONDS > dm.BLUNT_STUN_SECONDS


# --------------------------------------------------------------------------
# The design document says the same
# --------------------------------------------------------------------------

def unwrapped(text: str) -> str:
    return " ".join(text.split())


@pytest.fixture(scope="module")
def gdd() -> str:
    import pathlib

    root = pathlib.Path(__file__).resolve().parents[2]
    path = root / "docs" / "Cataclysm_GDD_v2.md"
    if not path.is_file():
        pytest.skip("the design document is not present")
    return unwrapped(path.read_text(encoding="utf-8"))


def test_the_design_document_has_the_section():
    """Standalone and not using the fixture, so deleting the section fails by
    name rather than erroring every test that shares a fixture."""
    import pathlib

    root = pathlib.Path(__file__).resolve().parents[2]
    path = root / "docs" / "Cataclysm_GDD_v2.md"
    if not path.is_file():
        pytest.skip("the design document is not present")
    assert "### **Stun and the Anti-Stun-Lock Rule**" in path.read_text(
        encoding="utf-8"), (
        "the design document no longer has the section stating the rule that "
        "stops a player being stun-locked. Issue #216.")


def test_the_document_states_the_threshold_the_model_uses(gdd):
    import re

    found = re.search(
        r"at least (\d+)% of the target's maximum health", gdd)
    assert found, (
        "the design document no longer states how much of a target's maximum "
        "health a hit has to take before it can stun")
    assert float(found.group(1)) == pytest.approx(dm.STUN_DAMAGE_THRESHOLD)


def test_the_document_states_the_immunity_window_the_model_uses(gdd):
    import re

    found = re.search(r"cannot be stunned again for (\d+) seconds", gdd)
    assert found, (
        "the design document no longer states how long a target is immune to "
        "stun after being stunned")
    assert float(found.group(1)) == pytest.approx(dm.STUN_IMMUNITY_SECONDS)


def test_the_document_states_that_bosses_are_immune(gdd):
    assert "**A boss cannot be stunned at all.**" in gdd


def test_the_document_says_a_designed_stun_skips_the_threshold(gdd):
    """Without this a reader would conclude Shield Bash fails against a healthy
    target, which is the opposite of what the skill is for."""
    assert ("A skill whose stated effect is to stun ignores the damage "
            "threshold") in gdd


def test_the_document_records_that_point_four_is_still_open(gdd):
    """The project owner answered three of the issue's four questions and
    deferred the fourth. A document that reads as complete would lose that."""
    assert "not yet decided" in gdd


# --- What the rule covers, said once ----------------------------------------
#
# Issue #296. The section used to say both that a slow is not covered by this
# rule and that whether a slow is covered is open, four lines apart. Both cannot
# be true. The first is the surviving statement, because the reason behind it is
# used elsewhere in the document: Cripple's slow caps below total precisely
# because a full stop would be a stun, which only makes sense if the two are
# governed separately.


def test_the_document_says_a_slow_is_not_covered(gdd):
    assert "A slow is not a stun and is not covered by this rule." in gdd, (
        "the design document no longer states that the anti-stun-lock rule does "
        "not cover slows. Issue #296.")


def test_the_document_does_not_also_say_a_slow_is_open(gdd):
    """The contradiction itself. This is the check that fails if it comes back."""
    assert "knockback and slow carry the same threshold" not in gdd, (
        "the design document says a slow's position is open, four lines after "
        "saying it is settled and not covered. Both cannot be true. Issue #296.")


def test_the_open_questions_are_about_gear_and_not_about_slows(gdd):
    """What is open is which affixes exist, and nothing about what the rule covers.

    THIS USED TO ASSERT THAT KNOCKBACK WAS OPEN, when the only settled thing was
    that a slow is not covered. Issue #297 then settled knockback too, by
    splitting it: a knockdown is covered in full and a displacement is not. So
    the sentence this checked is gone, and what remains open in this paragraph is
    only which crowd control affixes exist, which is #298 and #299.
    """
    assert "knockback carries the same threshold and window as stun" not in gdd, (
        "the design document says knockback's position is open. It was settled "
        "by issue #297: a knockdown is covered by the rule, a displacement is "
        "not.")
    assert "no affix grants a chance to stun and none scales a stun's duration" in gdd


def test_the_reason_a_slow_is_separate_is_still_in_the_document(gdd):
    """The cap on Cripple's slow is why a slow is governed separately.

    If this sentence goes, the surviving statement above loses its support and
    the question is genuinely open again rather than merely written twice.
    """
    assert ("Cripple's slow caps below total** because a full stop is a stun") in gdd, (
        "the design document no longer explains that Cripple's slow caps below "
        "total because a full stop would be a stun. That is the reason a slow "
        "is not covered by the anti-stun-lock rule. Issue #296.")


def test_the_open_questions_name_issues_that_are_not_the_closed_parent(gdd):
    """#270 was split into #296 to #300 and closed, so pointing at it is a dead end."""
    assert "Issue #270 carries it" not in gdd, (
        "the design document points at issue #270, which was split into #296, "
        "#297, #298, #299 and #300 and closed. Name the live children instead.")


# --- Knockdown is covered, displacement is not ------------------------------
#
# Issue #297. This project has two different effects under the one word
# "knockback". A knockdown stops the target acting for a stated number of
# seconds, which is what a stun does. A displacement moves it and lets it act on
# arrival. They get opposite answers, and the split is what the genre does:
# Diablo IV ships Knockback and Knock Down as separate effects.


def test_the_document_states_the_criterion_for_being_covered(gdd):
    """The project owner's rule, 2026-08-05: only hard stops are covered.

    Stated as a general test rather than as a list, so an effect added later has
    an answer without anyone having to decide it case by case.
    """
    assert ("An effect is covered when it completely stops the target operating "
            "any part of its character") in gdd, (
        "the design document no longer states the criterion for what the "
        "anti-stun-lock rule covers. Issue #297.")


def test_madness_is_recorded_as_open_rather_than_decided_by_omission(gdd):
    """It is the one case the criterion does not settle, and it is the longest
    hold in the game and the only one that is freely rollable. Issue #303."""
    assert "Issue #303" in gdd, (
        "the design document no longer records that Madness's position under the "
        "anti-stun-lock rule is open. Leaving it out reads as not covered, which "
        "is one of the two answers and has not been chosen.")


def test_the_document_says_a_knockdown_is_covered(gdd):
    assert "A knockdown is a hard stop, so it carries all three parts" in gdd, (
        "the design document no longer says a knockdown is covered by the "
        "anti-stun-lock rule. Issue #297.")


def test_the_document_says_a_displacement_is_not_covered(gdd):
    assert "Displacement is not covered, because it does not hold the target still." in gdd, (
        "the design document no longer says displacement is outside the "
        "anti-stun-lock rule. Issue #297.")


def test_the_document_states_what_limits_repeated_displacement(gdd):
    """WHAT THIS USED TO ASSERT. Until 2026-08-05 this was
    test_the_document_does_not_leave_displacement_sounding_unlimited, and it
    checked that the document admitted displacement still needed a limit that
    nobody had chosen — the sentence "This does not mean it should be repeatable
    without limit", pointing at issue #302.

    #302 is answered, so the property to hold is the rule itself. The admission
    would now be a promise the document has already kept."""
    text = unwrapped(gdd)
    assert "limited instead by diminishing distance" in text, (
        "the design document no longer states what limits repeated "
        "displacement. Issue #302 answered it on 2026-08-05: each displacement "
        "within 5 seconds moves the target half as far as the one before.")
    assert "half as far as the one before" in text, (
        "the document names a limit on repeated displacement without saying "
        "what the limit is. The rule is halving. Issue #302.")


def test_the_displacement_rule_reuses_the_stun_immunity_window(gdd):
    """One number, not two. The section already has a 5 second window for stun
    immunity, and the displacement count resets on the same 5 seconds. Reusing
    it is deliberate and the document says so, because a reader who finds two
    5-second windows should know they are the same one rather than a
    coincidence that could drift apart."""
    text = unwrapped(gdd)
    assert "The 5 seconds is the same 5 seconds" in text, (
        "the design document states a 5 second window for the displacement "
        "rule without saying it is the stun immunity window. Two independent "
        "5s are two numbers that can drift. Issue #302.")
    assert f"{dm.STUN_IMMUNITY_SECONDS:.0f} seconds" in text, (
        f"the model's stun immunity window is "
        f"{dm.STUN_IMMUNITY_SECONDS} seconds and the document no longer says "
        f"so where the displacement rule reuses it.")


def test_the_displacement_rule_exempts_no_boss_and_says_why(gdd):
    """The one place this rule deliberately differs from the stun rule above it.
    A boss cannot be stunned at all, because a boss held still is not a fight. A
    boss pushed four meters is still fighting, so the reason does not carry
    across and the exemption is not repeated.

    Without the reason stated, the difference reads as an oversight and the next
    reader adds boss immunity for symmetry."""
    text = unwrapped(gdd)
    assert "no boss exemption" in text, (
        "the design document does not say whether a boss is exempt from the "
        "displacement rule. The stun rule two paragraphs above makes bosses "
        "immune, so silence here reads as an oversight. Issue #302.")
    assert "a boss pushed four meters is still fighting" in text, (
        "the document exempts no boss from the displacement rule without "
        "giving the reason, which is the only thing distinguishing it from the "
        "stun rule's boss immunity. Issue #302.")


def test_the_document_says_why_distance_rather_than_immunity(gdd):
    """CLAUDE.md requires a design decision to cite how the genre does it. Two
    shipped games solved this differently and the document takes the axis from
    one and the escalation from the other, so both belong in the text: Path of
    Exile 2 treats knockback distance as a scalar on both sides, Diablo IV
    escalates a hidden resistance to a hard immunity threshold."""
    text = unwrapped(gdd)
    assert "Why distance rather than immunity" in text, (
        "the design document states the halving rule without saying why it is "
        "a curve rather than the immunity threshold Diablo IV ships. That is "
        "the one real alternative and it needs answering. Issue #302.")
    for game in ("Path of Exile 2", "Diablo IV"):
        assert game in text, (
            f"the document's argument for diminishing distance no longer "
            f"cites {game}. The argument is that the genre has two answers and "
            f"this takes one axis from each.")


def test_the_document_shows_the_rule_prevents_the_failure_it_was_written_for(gdd):
    """A rule with no worked example is a rule nobody can check. The failure
    named on issue #302 was "a target held permanently at the far end of a
    room", and the document does the arithmetic that shows halving prevents
    it."""
    text = unwrapped(gdd)
    assert "cannot produce the failure it was written for" in text, (
        "the design document states the halving rule without showing it stops "
        "the thing it was written to stop. Issue #302 named that thing: a "
        "target held permanently at the far end of a room.")
    assert "4 meters, then 2, then 1" in text, (
        "the worked example is gone. Three displacements inside the window "
        "move a target seven meters in total, which is the arithmetic that "
        "makes the rule checkable. Issue #302.")


class TestTheDisplacementRuleMatchesTheSkillTable:
    """The rule's central argument is a fact about the skill list, so it is read
    from `game/Data/WeaponSkills.csv` rather than asserted.

    The argument is that a hard immunity threshold is unnecessary because no
    displacing skill can be repeated quickly: all of them are Heavy attacks,
    which are slow by design, or Movement skills, which go on cooldown. If a
    displacing skill ever appears in another slot that argument weakens, and
    `docs/DECISIONS.md` says outright that the decision should be re-read when
    it does. These tests are what makes that happen.
    """

    #: The slots a displacing skill may be in for the rule's argument to hold.
    SLOTS_THAT_CANNOT_BE_SPAMMED = {"Heavy", "Movement"}

    def _rows(self):
        import csv
        import pathlib

        root = pathlib.Path(__file__).resolve().parents[2]
        path = root / "game" / "Data" / "WeaponSkills.csv"
        if not path.is_file():
            pytest.skip("the generated weapon skill table is not present")
        with path.open(encoding="utf-8-sig") as handle:
            return list(csv.DictReader(handle))

    def _displacing(self) -> dict[str, str]:
        """Skill name to slot, for skills that push or shove a target.

        Three things say "knock" and are not a displacement, and each is
        excluded for its own reason. "cannot be knocked back" and "immune to"
        are a skill preventing displacement rather than applying it; "knocking
        their weapon aside" is Flaying Lash's disarm; "knocked down" is the
        knockdown, which the rule above covers instead.
        """
        import re

        out: dict[str, str] = {}
        for row in self._rows():
            text = row["SkillDescription"]
            for found in re.finditer(r"knock\w*", text, re.I):
                before = text[max(0, found.start() - 40):found.start()]
                after = text[found.start():found.start() + 60]
                if re.search(r"cannot be|immune to", before, re.I):
                    continue
                if re.search(r"their weapon", after, re.I):
                    continue
                if re.search(r"^knock\w*\s+(?:them\s+|enemies\s+)?down",
                             after, re.I):
                    continue
                out[row["SkillName"]] = row["Slot"]
                break
        return out

    def test_some_skill_actually_displaces(self):
        """Guards every test below from passing on an empty set."""
        assert self._displacing(), (
            "no skill in game/Data/WeaponSkills.csv displaces a target any "
            "more, so the displacement rule in the design document governs "
            "nothing. Either a skill was reworded or the rule can go.")

    def test_every_displacing_skill_is_in_a_slot_that_cannot_be_spammed(self):
        """The load-bearing fact. The document argues no hard immunity is
        needed BECAUSE no displacing skill can be repeated quickly. A displacing
        skill in a fast slot makes that argument false, and the halving curve
        may then be too weak."""
        wrong = {name: slot for name, slot in self._displacing().items()
                 if slot not in self.SLOTS_THAT_CANNOT_BE_SPAMMED}
        assert not wrong, (
            f"these displacing skills are outside the Heavy and Movement "
            f"slots: {wrong}. docs/Cataclysm_GDD_v2.md argues that repeated "
            f"displacement needs no immunity threshold because every "
            f"displacing skill is either a slow Heavy attack or a Movement "
            f"skill on cooldown. That argument is now false, and "
            f"docs/DECISIONS.md says the decision should be re-read when it "
            f"is. Issue #302.")

    def test_the_document_states_the_number_of_displacing_skills_it_found(self):
        """The document says "all nine displacing skills". A count in prose goes
        stale silently, so it is checked. Changing the skill list means changing
        the sentence, which is the point."""
        import pathlib
        root = pathlib.Path(__file__).resolve().parents[2]
        gdd = (root / "docs" / "Cataclysm_GDD_v2.md").read_text(encoding="utf-8")
        count = len(self._displacing())
        assert f"all {NUMBER_WORDS[count]} displacing skills" in unwrapped(gdd), (
            f"game/Data/WeaponSkills.csv has {count} displacing skills and "
            f"docs/Cataclysm_GDD_v2.md does not say so. Issue #302.")

    def test_nothing_in_the_game_can_displace_the_player(self):
        """The document claims this, and it is the reason issue #310 exists. If
        an enemy modifier or status effect ever displaces the player, the
        sentence saying nothing does becomes false and #310 is answered."""
        import pathlib
        import re

        root = pathlib.Path(__file__).resolve().parents[2]
        found: dict[str, list[str]] = {}
        for name in ("EnemyModifiers.csv", "StatusEffects.csv"):
            path = root / "game" / "Data" / name
            if not path.is_file():
                continue
            with path.open(encoding="utf-8-sig") as handle:
                hits = [line for line in handle
                        if re.search(r"knock\w*\s+(?:back|aside)", line, re.I)]
            if hits:
                found[name] = hits
        assert not found, (
            f"something can now displace the player: {found}. "
            "docs/Cataclysm_GDD_v2.md says nothing in the game knocks the "
            "player back, which is why Living Pyre, Unstoppable Force and "
            "Forge Stance spend part of their text on immunity to it. Issue "
            "#310 is answered; update the document and delete this test.")


def test_the_decision_log_records_the_displacement_reasoning(gdd):
    """The design document states rules; the reasoning lives in the decision
    log. This one needs it more than most, because the argument rests on a fact
    about the current skill list and a later reader has to be able to see that
    and re-check it."""
    import pathlib
    root = pathlib.Path(__file__).resolve().parents[2]
    log = (root / "docs" / "DECISIONS.md").read_text(encoding="utf-8")
    heading = ("## 2026-08-05 — Repeated displacement is limited by halving its "
               "distance, not by immunity")
    assert heading in log, (
        "docs/DECISIONS.md has no entry for issue #302. The rule is in the "
        "design document; the reasoning belongs here.")
    entry = log[log.index(heading):]
    entry = entry[:entry.index("\n---", 1)] if "\n---" in entry[1:] else entry
    assert "What argues against it" in entry, (
        "the entry records only the case for diminishing distance. Diablo IV "
        "is the one game that actually solved this and it chose a hard "
        "threshold instead, which is a real argument the log should carry.")
    assert "should be re-read" in entry, (
        "the entry does not say under what condition the decision stops "
        "holding. It rests on every displacing skill being in a slow slot, so "
        "that is the condition. Issue #302.")
    assert entry.count("https://") >= 5, (
        "the entry makes claims about three shipped games and cites fewer than "
        "five sources.")
    assert "search result summaries" in entry, (
        "the entry presents its sources without saying none was fetched. "
        "Stating that is the project rule when the evidence is second-hand.")


def test_knockdown_and_stun_share_one_immunity_window(gdd):
    """One each would allow the alternation the window exists to stop."""
    assert "share one window rather than one each" in gdd, (
        "the design document no longer says stun and knockdown share a single "
        "immunity window. Two 3 second holds taken in turn is the failure the "
        "window exists to prevent. Issue #297.")


class TestTheDocumentMatchesTheSkillTable:
    """The split is only right if the skills really do divide that way.

    Read from `game/Data/WeaponSkills.csv`, the generated table of weapon
    skills, so a skill added later with a knockdown longer than the document
    claims makes this fail rather than quietly making the document wrong.
    """

    def _skills(self):
        import csv
        import pathlib

        root = pathlib.Path(__file__).resolve().parents[2]
        path = root / "game" / "Data" / "WeaponSkills.csv"
        if not path.is_file():
            pytest.skip("the generated weapon skill table is not present")
        with path.open(encoding="utf-8-sig") as handle:
            return list(csv.DictReader(handle))

    def _durations(self, pattern: str) -> dict[str, float]:
        import re

        out: dict[str, float] = {}
        for row in self._skills():
            found = re.search(pattern, row["SkillDescription"], re.I)
            if found:
                out[row["SkillName"]] = float(found.group(1))
        return out

    def test_some_skill_actually_knocks_down(self):
        """Guards every test below from passing on an empty set."""
        assert self._durations(r"knocked down for ([\d.]+) second"), (
            "no skill in game/Data/WeaponSkills.csv knocks down any more, so "
            "the knockdown half of the anti-stun-lock rule covers nothing. "
            "Either a skill was renamed or the rule can be simplified.")

    def test_every_knockdown_is_longer_than_every_skill_stun(self):
        """The document's argument rests on this, so it is checked, not asserted.

        If a knockdown were shorter than the stuns, leaving it outside the rule
        would be arguable. It is not: the shortest knockdown is longer than the
        longest stun a skill grants.
        """
        knockdowns = self._durations(r"knocked down for ([\d.]+) second")
        stuns = self._durations(r"stun\w*[^.]*?for ([\d.]+) second")
        assert stuns, "no skill states a stun duration any more"
        assert min(knockdowns.values()) > max(stuns.values()), (
            f"knockdowns run {sorted(knockdowns.values())} and skill stuns run "
            f"{sorted(stuns.values())}. The design document argues a knockdown "
            "must be covered by the anti-stun-lock rule because it is the "
            "longest hold in the game. That is no longer true. Issue #297.")

    def test_no_knockdown_is_shorter_than_the_blunt_sub_type_stun(self):
        """0.75s is the shortest designed hold. A knockdown under it would be a
        different kind of effect and would need its own paragraph."""
        knockdowns = self._durations(r"knocked down for ([\d.]+) second")
        assert min(knockdowns.values()) >= dm.BLUNT_STUN_SECONDS

    def test_displacement_and_knockdown_are_different_skills(self):
        """If one skill did both, the document would need to say which wins."""
        import re

        both = [row["SkillName"] for row in self._skills()
                if re.search(r"knocked down for", row["SkillDescription"], re.I)
                and re.search(r"knock\w*\s+(?:them\s+|enemies\s+)?back",
                              row["SkillDescription"], re.I)]
        assert not both, (
            "these skills both displace and knock down, and the design document "
            f"treats those as separate effects with opposite rules: {both}. "
            "Issue #297.")
