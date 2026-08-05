"""How an ailment is made stronger, checked against the document that says so.

WHAT THIS FILE IS ABOUT. There is one way an ailment is made stronger today, and
it is not a stat. `docs/Cataclysm_GDD_v2.md`, under "Applying Damage Over Time
and Other Effects", decides it:

    Chance to apply caps at 100%. Everything above it becomes magnitude instead.

So 800% chance to apply bleed applies bleed on every hit at eight times its
magnitude. The document then says, per effect, what magnitude scales: the damage
for a damage over time effect, a capped strength and then the duration for a
slow, the duration alone for an effect with no strength axis.

Nothing in the repository connected that statement to `ailment_application` in
`sim/cataclysm_sim/affixes.py`, which implements it. This file is that
connection, in the direction that fails loudly.

WHY IT WAS WRITTEN. Issue #205 reported that damage over time and ailments can be
applied but never made stronger, having cross-referenced the affix table's `Stat`
column and found nothing that raises an ailment's damage, strength or duration.
The rule above is a relationship between two numbers rather than a row in a
table, so a data-level reading cannot find it. Pinning it is what stops the third
report of the same thing.

THIS FILE DOES NOT CLOSE #205, AND MUST NOT BE READ AS SETTLING IT. The project
owner decided on 2026-08-04 that the overflow rule is not enough on its own and
that the pool needs flat damage-over-time damage, duration and chance affixes as
well, plus more sources of tick frequency than the single existing affix. See
decision 4 of the "Nine decisions from an audit of the affix pool" entry in
`docs/DECISIONS.md`. That work is still open.

NOTHING HERE BLOCKS IT. Every assertion below is about the overflow rule and
about which effects are damage over time. None of them says a scaling stat may
not exist, so adding those affixes does not fail this file.

WHAT TICK RATE DOES WAS DECIDED AFTER THIS FILE WAS WRITTEN, and it is now
asserted here. Issue #220 asked whether a damage over time effect deals a fixed
amount per tick or a total spread across a duration, because "increased damage
over time frequency" means opposite things under the two readings. The project
owner answered on 2026-08-04: **a fixed amount per tick**, so ticking faster deals
more total damage, and damage per tick, tick rate and duration are three separate
metrics that all multiply the same output.

That is the opposite of Path of Exile and Last Epoch, which both treat ticking as
delivery and leave the total alone. The departure is deliberate and the reasoning
and sources are in `docs/DECISIONS.md`. It is asserted here because the reading
decides whether one shipped affix value is a damage multiplier or a convenience,
and the document is the only place that says which.

WHAT IS STILL WRONG AND IS NOT THIS FILE'S JOB. The 12% on that affix was set
under the other reading and is known to be mis-priced. It is not corrected here,
because two of the three levers do not exist yet and the three have to be priced
together. Issue #258, blocked on #205.

NOTHING HERE IS A SECOND COPY OF THE NUMBERS. Every expected value is parsed out
of the design document. Changing the design in `Cataclysm_GDD_v2.md` is what
changes what this file expects, which is the only arrangement that cannot drift.
"""

from __future__ import annotations

import csv
import pathlib
import re

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GDD = REPO_ROOT / "docs" / "Cataclysm_GDD_v2.md"
STATUS_EFFECT_CSV = REPO_ROOT / "game" / "Data" / "StatusEffects.csv"

SECTION = "### **Applying Damage Over Time and Other Effects**"

#: The sentence the whole rule rests on. Quoted exactly so that softening it in
#: the design document fails here rather than silently leaving the code alone.
THE_RULE = ("**Chance to apply caps at 100%. Everything above it becomes "
            "magnitude instead.**")

#: What the second cell of a worked example row says happened, in
#: `| 250% | Applies on every hit, at 2.5 times its magnitude |`.
APPLIES_ON_A_SHARE = re.compile(r"Applies on (\d+(?:\.\d+)?)% of hits")
APPLIES_ALWAYS = re.compile(r"Applies on every hit")
AT_NORMAL_MAGNITUDE = re.compile(r"at its normal magnitude")
AT_A_MULTIPLE = re.compile(r"at (\d+(?:\.\d+)?) times its magnitude")

#: The line introducing the per-effect table.
EFFECT_TABLE_MARKER = "**The effects a player can apply**"

#: A cell that is a heading rather than data. The design document was exported
#: with its bold markers escaped, so a heading cell reads `\*\*Effect\*\*`. A
#: separator row is all colons and dashes, or all empty.
HEADING_CELL = re.compile(r"^\\?\*\\?\*")
SEPARATOR_CELL = re.compile(r"^[:\-]*$")


def section_text() -> str:
    """The one section of the design document this file is about.

    Bounded rather than searching the whole file, so a table elsewhere in a
    3,000-line document cannot be mistaken for one of these two. The bound is
    any heading at all, not only a `##` or `###` one: this section is the last
    of its part, and the next heading is the single-hash `# **V. Skill System**`.
    """
    if not GDD.is_file():
        pytest.skip("the design document is not present")
    body = GDD.read_text(encoding="utf-8")
    start = body.find(SECTION)
    assert start != -1, f"{GDD.name} no longer has a section headed {SECTION!r}"

    after = body[start + len(SECTION):]
    ends = [m.start() for m in re.finditer(r"^#{1,6} ", after, re.MULTILINE)]
    return after[:ends[0]] if ends else after


def cells(row: str) -> list[str]:
    """A markdown table row's cells, without the leading and trailing pipes."""
    return [cell.strip() for cell in row.strip().strip("|").split("|")]


def table_after(section: str, marker: str) -> list[list[str]]:
    """The data rows of the first markdown table below a line of text.

    ANCHORED TO THE MARKER rather than taken from the whole section, because the
    section holds three tables and matching on shape alone picked up the wrong
    one. Header and separator rows are dropped, so what comes back is data.
    """
    lines = section.splitlines()
    index = next((i for i, line in enumerate(lines) if marker in line), None)
    assert index is not None, f"{GDD.name} no longer contains {marker!r}"

    rows: list[list[str]] = []
    started = False
    for line in lines[index + 1:]:
        stripped = line.strip()
        if stripped.startswith("|"):
            started = True
            row = cells(stripped)
            if any(HEADING_CELL.match(c) for c in row):
                continue
            if all(SEPARATOR_CELL.match(c) for c in row):
                continue
            rows.append(row)
        elif started and stripped:
            break
    return rows


@pytest.fixture(scope="module")
def section() -> str:
    return section_text()


@pytest.fixture(scope="module")
def model():
    from cataclysm_sim import affixes as af
    return af


def worked_examples(section: str) -> list[tuple[float, float, float]]:
    """The example table, as (chance, chance applied, magnitude multiplier).

    Read from the prose in the second cell rather than from a second copy of the
    numbers, so the table is the only place they are written.
    """
    out = []
    for row in table_after(section, THE_RULE):
        assert len(row) == 2, f"the worked example table has a row of {row}"
        match = re.fullmatch(r"(\d+(?:\.\d+)?)%", row[0])
        assert match, f"{row[0]!r} is not a percentage chance"
        chance, says = float(match.group(1)), row[1]

        share = APPLIES_ON_A_SHARE.search(says)
        if share:
            applied = float(share.group(1))
        elif APPLIES_ALWAYS.search(says):
            applied = 100.0
        else:
            pytest.fail(f"the row for {chance}% does not say how often it "
                        f"applies: {says!r}")

        multiple = AT_A_MULTIPLE.search(says)
        if multiple:
            magnitude = float(multiple.group(1))
        elif AT_NORMAL_MAGNITUDE.search(says):
            magnitude = 1.0
        else:
            pytest.fail(f"the row for {chance}% does not say what happens to "
                        f"its magnitude: {says!r}")

        out.append((chance, applied, magnitude))
    return out


def effect_table(section: str) -> dict[str, tuple[str, str]]:
    """The per-effect table, as effect name to (what it does, magnitude scales).

    Anchored to the line that introduces it. The section holds two other tables
    and an earlier version of this file, which matched on shape alone, read rows
    from all three.
    """
    out: dict[str, tuple[str, str]] = {}
    for row in table_after(section, EFFECT_TABLE_MARKER):
        assert len(row) == 3, f"the effects table has a row of {row}"
        effect, does, scales = row
        out[effect] = (does, scales)
    return out


class TestTheDesignDocumentStillSaysIt:
    """Everything below reads the design document. This is what says it is there."""

    def test_the_overflow_rule_is_stated(self, section):
        assert THE_RULE in section, (
            f"{GDD.name} no longer states the rule that chance to apply caps at "
            "100% and the excess becomes magnitude. `ailment_application` in "
            "sim/cataclysm_sim/affixes.py implements it and would be orphaned.")

    def test_there_are_worked_examples_to_check(self, section):
        assert len(worked_examples(section)) >= 4

    def test_the_overflow_is_stated_to_be_useful(self, section):
        """The reason the rule exists, not only the rule.

        Without it a build stacking ailment chance hits the cap and every point
        past it is dead. That reasoning is what stops the cap being 'simplified'
        back to a waste later.
        """
        assert "**Why the overflow is not simply wasted.**" in section


class TestTheCodeMatchesTheWorkedExamples:
    """`ailment_application` against every row of the design document's table."""

    def test_every_worked_example(self, section, model):
        for chance, applied, magnitude in worked_examples(section):
            got_applied, got_magnitude = model.ailment_application(chance)
            assert got_applied == pytest.approx(applied), (
                f"at {chance}% chance the design document says the effect "
                f"applies on {applied}% of hits; affixes.py says {got_applied}")
            assert got_magnitude == pytest.approx(magnitude), (
                f"at {chance}% chance the design document says {magnitude} times "
                f"magnitude; affixes.py says {got_magnitude}")

    def test_the_cap_in_the_code_is_the_cap_in_the_document(self, model):
        assert model.AILMENT_CHANCE_CAP == 100.0

    def test_one_stack_only(self, model):
        """The rule only means something because a second stack is impossible.

        If an enemy could carry two stacks of bleed, chance above 100% would
        naturally become a second application and there would be nothing to
        overflow into magnitude. Last Epoch does exactly that; this design does
        not, and the two choices are alternatives rather than companions.
        """
        assert model.MAX_STACKS_ON_AN_ENEMY == 1
        assert "**An enemy carries at most one stack of any effect the player " \
               "applies.**" in section_text()

    def test_magnitude_is_never_below_one_and_chance_never_above_the_cap(self, model):
        for chance in (0.0, 1.0, 50.0, 99.9, 100.0, 100.1, 250.0, 800.0, 5000.0):
            applied, magnitude = model.ailment_application(chance)
            assert applied <= model.AILMENT_CHANCE_CAP
            assert magnitude >= 1.0

    def test_a_negative_chance_is_rejected(self, model):
        with pytest.raises(ValueError, match="not a chance"):
            model.ailment_application(-1.0)


class TestEveryEffectSaysWhatMagnitudeDoesToIt:
    """The rule is worthless for an effect that does not say what it scales."""

    def test_the_table_has_a_row_for_every_ailment_affix(self, section, model):
        listed = effect_table(section)
        applied_by_gear = {a.ailment for a in model.AILMENT_AFFIXES}
        missing = sorted(applied_by_gear - set(listed))
        assert not missing, (
            f"{GDD.name} lists no magnitude behaviour for {missing}, which gear "
            "can apply. An effect gear can apply and the design does not scale "
            "is exactly what issue #205 reported.")

    def test_the_table_lists_nothing_gear_cannot_apply(self, section, model):
        listed = effect_table(section)
        applied_by_gear = {a.ailment for a in model.AILMENT_AFFIXES}
        extra = sorted(set(listed) - applied_by_gear)
        assert not extra, (
            f"{GDD.name} calls {extra} player-applicable, but no affix applies "
            "them. That is the shape of issue #152, where burn was designed and "
            "unreachable from gear for a month.")

    def test_every_row_says_what_magnitude_scales(self, section):
        for effect, (does, scales) in effect_table(section).items():
            assert does, f"{effect} has no description"
            assert scales, f"{effect} does not say what magnitude scales"

    def test_every_row_scales_damage_a_reduction_or_a_duration(self, section):
        """The three shapes the design document allows, and no fourth.

        A row saying magnitude scales something else means either a new shape
        that the rule above it does not cover, or a typo.
        """
        allowed = ("the damage", "the reduction", "the duration", "the strength")
        for effect, (_, scales) in effect_table(section).items():
            assert scales.lower().startswith(allowed), (
                f"{effect} scales {scales!r}, which is none of {allowed}")


class TestWhichEffectsAreDamageOverTime:
    """Three places name the damaging ailments, and all three must agree."""

    def test_the_document_and_the_model_agree(self, section, model):
        damaging = {effect for effect, (_, scales) in effect_table(section).items()
                    if scales.lower().startswith("the damage")}
        assert damaging == set(model.DAMAGE_OVER_TIME_AILMENTS), (
            f"only in {GDD.name}: "
            f"{sorted(damaging - set(model.DAMAGE_OVER_TIME_AILMENTS))}; "
            "only in affixes.DAMAGE_OVER_TIME_AILMENTS: "
            f"{sorted(set(model.DAMAGE_OVER_TIME_AILMENTS) - damaging)}")

    def test_the_table_the_game_loads_agrees(self, section, model):
        """Each damaging ailment is EffectKind "DoT", and no other one is.

        `game/Data/StatusEffects.csv` is generated from the workbook and is what
        the game reads. An effect the design scales as damage and the data calls
        a debuff would take its magnitude on the wrong axis.
        """
        if not STATUS_EFFECT_CSV.is_file():
            pytest.skip("StatusEffects.csv not present")
        with STATUS_EFFECT_CSV.open(newline="", encoding="utf-8-sig") as handle:
            kinds = {row["EffectName"].strip(): row["EffectKind"].strip()
                     for row in csv.DictReader(handle)}

        for effect, (_, scales) in effect_table(section).items():
            assert effect in kinds, (
                f"{effect} is in the design document and not in "
                f"{STATUS_EFFECT_CSV.name}")
            expected = "DoT" if scales.lower().startswith("the damage") else "Debuff"
            assert kinds[effect] == expected, (
                f"{effect} scales {scales!r} so it should be a {expected} in "
                f"{STATUS_EFFECT_CSV.name}, which calls it a {kinds[effect]}")


class TestDamageOverTimeIsPerTick:
    """Issue #220. Which of the two readings the document states, and that its
    own worked example is arithmetically what it claims.

    A sentence saying "fixed amount per tick" is easy to write and easy to
    soften back into ambiguity, which is what left this undecided for as long as
    it was. The example is checked as well as the sentence, because an example
    that does not multiply out is how a reader learns the sentence was not meant.
    """

    #: The sentence the whole reading rests on, quoted exactly.
    THE_RULE = "**A damage over time effect deals a fixed amount per tick.**"

    #: The three levers, and the word the document uses for each.
    LEVERS = ("Damage per tick", "Tick rate", "Duration")

    def test_the_document_says_which_of_the_two_readings_it_is(self, section):
        assert self.THE_RULE in section, (
            "the design document no longer states that a damage over time "
            "effect deals a fixed amount per tick. Without it, 'increased "
            "damage over time frequency' means either a damage multiplier or a "
            "convenience and nothing says which. Issue #220.")

    def test_it_rules_out_the_other_reading_by_name(self, section):
        """Saying what it is leaves a reader who arrived from Path of Exile
        assuming the total is fixed. The document says it is not."""
        assert "It is not a total handed out in instalments." in section

    def test_all_three_levers_are_named_and_all_three_raise_the_total(
            self, section):
        for lever in self.LEVERS:
            row = next((line for line in section.splitlines()
                        if line.strip().strip("|").split("|")[0].strip()
                        == lever), None)
            assert row is not None, (
                f"the design document does not list {lever} as a damage over "
                f"time metric. Issue #220 says there are three.")
            assert row.strip().endswith("Rises |"), (
                f"{lever} no longer raises the total damage of a damage over "
                f"time effect. All three do, which is the whole decision.")

    def test_the_worked_example_multiplies_out(self, section):
        """20 damage per tick, once per second, 5 seconds, 100 total."""
        found = re.search(
            r"deals (\d+) damage per tick, ticks once per second and lasts "
            r"(\d+) seconds deals (\d+) damage in total", section)
        assert found, (
            "the design document's damage over time example is gone or "
            "reworded. It is what makes the rule unambiguous. Issue #220.")
        per_tick, seconds, total = (int(g) for g in found.groups())
        assert per_tick * seconds == total, (
            f"{per_tick} per tick for {seconds} seconds is "
            f"{per_tick * seconds}, but the document says {total}")

    def test_it_states_that_the_three_multiply_and_the_figure_is_right(
            self, section):
        """A reader who assumes the three add gets 2.44 where the rule gives
        3.24, which is the difference between a build working and not."""
        found = re.search(
            r"with (\d+)% more of each does not deal (\d+)% of the base total; "
            r"it deals [\d.]+ × [\d.]+ × [\d.]+, which is (\d+)%", section)
        assert found, (
            "the design document no longer says what happens when all three "
            "damage over time levers are raised together. They multiply, and "
            "that is the consequence of the rule most likely to surprise. "
            "Issue #220.")
        each, added, multiplied = (int(g) for g in found.groups())
        assert added == 100 + each
        assert round((1 + each / 100) ** 3 * 100) == multiplied, (
            f"{each}% on each of three multiplying levers is "
            f"{(1 + each / 100) ** 3 * 100:.0f}% of base, not {multiplied}%")

    def test_it_says_the_departure_from_the_genre_is_deliberate(self, section):
        """Both games surveyed do the opposite. A reader who knows that needs to
        see it was a choice, or they will file it as a mistake -- which is
        exactly how issue #220 came to be written."""
        assert "Path of Exile" in section and "Last Epoch" in section
        assert "deliberate departure" in section

    def test_the_model_records_that_the_shipped_value_is_known_wrong(self):
        """`INCREASED_DOT_FREQUENCY` is 12.0, priced as a convenience. Under
        this rule it is a damage multiplier. Leaving a wrong number with nothing
        saying so is how it survives into tuning as if it were considered."""
        from cataclysm_sim import affixes

        source = pathlib.Path(affixes.__file__).read_text(encoding="utf-8")
        start = source.index("INCREASED_DOT_FREQUENCY = StatAffix")
        comment = source[:start]
        assert "#258" in comment[-900:], (
            "sim/cataclysm_sim/affixes.py no longer records that "
            "INCREASED_DOT_FREQUENCY's value predates the issue #220 answer and "
            "is being re-priced under issue #258. Either the value was fixed, "
            "in which case delete this test and close #258, or the note was "
            "lost.")
        assert affixes.INCREASED_DOT_FREQUENCY.top_value == 12.0, (
            "INCREASED_DOT_FREQUENCY has moved off 12.0. If it was re-priced "
            "under issue #258, remove the comment saying it is wrong and this "
            "test with it.")
