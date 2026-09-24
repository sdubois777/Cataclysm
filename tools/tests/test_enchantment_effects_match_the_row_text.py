"""Every authored enchantment effect matches the words of its enchantment.

WHY THIS EXISTS. Issue #45. An enchantment says what it does in a sentence
written for a player -- "Double your energy shield" -- and carries no stat name
and no number a machine can read. The Enchantment Effects sheet of
`docs/All_Things_Cataclysm.xlsx` is where those are written, and
`tools/generate_datatables.py` turns it into `game/Data/EnchantmentEffects.csv`.

**That is two statements of one fact in two files.** The generator already
refuses a row whose `Effect` cell does not repeat its enchantment's words
exactly, so an enchantment that is reworded cannot keep its old numbers without
anyone noticing. What that cannot catch is a number typed wrong in the first
place, and that is what this file checks.

WHAT IS ASSERTED HERE.

    every effect names an enchantment that exists
    a set with any effect row is written whole: its first bonus and its
      drawback, which turn on together at the same threshold
    the sets written are the ones counted here, so a set only starts working
      when somebody means it to
    a row stating a range states one its enchantment's words state, in their
      order and with one sign, because the game shows the item's number in
      place of that range
    a row stating one value finds it in its enchantment's words outside any
      range, as a number or as the multiplying word the sentence uses
      ("Double", "tripled")
    every condition value appears in those words too
    an effect in the `more` bucket is on a sentence worded as a multiplier, and
      one in the `increased` bucket on a sentence worded as an increase
    an effect that removes its stat states 1 and is on a sentence saying the
      stat is gone, with no, cannot, can't or zero
    a negative value is on a sentence that takes something away, or, on
      crowd_control_resistance alone, on one saying the effect lasts longer
    a sentence stating no number is excused from the checks that need one
      only while docs/DECISIONS.md records the number chosen for it
    the two enchantment tables state as many ranges as were measured, which
      holds the generator's reader and the game's reader to one answer
    the coverage is what it is measured to be, so it only moves deliberately

WHAT THIS CANNOT CHECK is whether the stat chosen is the right stat. Nothing
automatic can read "Double your life leech" and know that `life_leech` rather
than `mana_leech` is meant, which is why the sheet is written by hand and
reviewed. `test_passive_effects_match_the_node_text.py` is the same check for
the passive trees.
"""

from __future__ import annotations

import csv
import pathlib
import re
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = REPO_ROOT / "game" / "Data"
EFFECTS_CSV = DATA / "EnchantmentEffects.csv"
ENCHANTMENT_CSVS = (DATA / "EnchantmentsPositive.csv",
                    DATA / "EnchantmentsNegative.csv")

sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_datatables as gen  # noqa: E402

#: Words that state a `more` multiplier without a number, and the value each
#: one means. "Double your energy shield" is +100% more; "tripled" is +200%.
MULTIPLYING_WORDS = {"double": 100.0, "doubled": 100.0, "twice": 100.0,
                     "triple": 200.0, "tripled": 200.0, "quadrupled": 300.0,
                     "halved": -50.0}

#: A sentence worded as a multiplier, which is what the `more` bucket is for.
MULTIPLIER = re.compile(
    r"\b(more|less|double|doubled|twice|triple|tripled|quadrupled|halved|slowed|ignore|ignores)\b",
    re.IGNORECASE)

#: A sentence worded as an increase, which is the `increased` bucket.
#: THE VERB IN ALL THREE TENSES, not only the past participle. 13 enchantment
#: sentences say "reduces" and 9 say "reduce" against 31 that say "reduced", and
#: until 2026-09-14 not one of the first two had an effect row -- so this pattern
#: accepted the meaning and refused two of its tenses, and nothing could notice.
#: `Negative_Taking_a_hit_reduces_your_damage_by_5_10_for_3` is the row that
#: exposed it. THIS WIDENS THE TENSE AND NOT THE MEANING: a sentence admitted by
#: one form is admitted by the others for exactly the same reason, so the check
#: is no weaker than it was.
INCREASE = re.compile(
    r"\b(increase|increases|increased|reduce|reduces|reduced|faster|slower|longer"
    r"|larger|gain|lose)\b"
    # AND "bonus", BUT NEVER THE SET LABEL. Measured 2026-09-18: 13
    # sentences use it as an effect word, 39 carry it ONLY inside
    # "(N-Piece Bonus)" where it names a set and claims nothing about a
    # number, and 3 carry both. A plain word match would let a set label
    # satisfy this rule.
    r'|(?<!-Piece )\bbonus\b',
    re.IGNORECASE)

#: A sentence that takes something away, which is where a negative value goes.
#: The same three tenses, for the same reason as above.
#:
#: `drain`, `drains` AND `drained` ADDED ON 2026-09-14, a labelled judgement
#: recorded in docs/DECISIONS.md. This one widens the MEANING and not a tense:
#: no word already here means it. Four enchantment sentences use it, and the
#: only one with an effect row -- "Using your ultimate ability drains 20%-40%
#: of your maximum HP" -- is written as a POSITIVE added_health_cost, so it
#: never reaches this pattern and nothing could notice the word missing.
#: "Dodging an attack drains 5%-10% of your class resource" is the first row
#: that will be negative on a sentence using it.
TAKING = re.compile(
    r"\b(less|reduce|reduces|reduced|lose|slower|shorter|halved|slowed"
    r"|drain|drains|drained|ignore|ignores)\b",
    re.IGNORECASE)

#: WORDS ADDED ON 2026-09-11 FOR THE RANGED ROWS, a labelled judgement recorded
#: in docs/DECISIONS.md. "Gain 20%-50% movespeed" and "Lose 5%-15% attack speed"
#: are this genre's wording for an increase and a reduction, and "You are
#: permanently slowed by 10%-20%" is a multiplier that takes speed away. Adding
#: them loosens the three patterns above, which is why the decision is written
#: down rather than only made here.

#: A stat on which a negative value makes what the character suffers last
#: LONGER, so its sentence says "longer" rather than a word that takes away.
#: "CC effects applied to you last 40%-70% longer" is a negative
#: `crowd_control_resistance`. Accepted on this stat only: on a duration, a
#: negative value beside "longer" would be a sign error, and is still refused.
LONGER_WHEN_NEGATIVE = {"crowd_control_resistance"}
LONGER = re.compile(r"\blonger\b", re.IGNORECASE)

#: A sentence saying a stat is gone, which is what the `removed` kind is for.
#: Issue #1791. The eleven sentences it was written for say it four ways: "You
#: have no armor" and "You no longer regenerate mana", "Cannot block", "Can't
#: regen your hp", and "Your maximum mana is reduced to zero". A removal states
#: no number, so these words are the only thing in the sentence to hold it to.
#:
#: WHOLE WORDS, so that "Nobody" and "cannon" do not read as a removal.
#: `test_a_removal_is_read_from_whole_words_only` holds that on made-up
#: sentences.
#:
#: `disabled` ADDED ON 2026-09-23, a labelled judgement made under the owner's
#: delegation, with the `drain` widening of 2026-09-14 as its precedent. It widens
#: the MEANING by one word that says a thing is gone: "HP regeneration is disabled
#: during combat". Swept first: two sentences use it, both removals -- that one,
#: and "Your own ultimate ability is disabled", which has no row.
REMOVING = re.compile(r"\b(no|cannot|can't|zero|does not|disabled)\b", re.IGNORECASE)

#: Stats whose value is a yes or a no rather than a quantity, so the sentence
#: states no number for it and should not. `skill_locked` above zero means the
#: character's skills are refused; the sentence says what happens and the 1 says
#: that it happens. Issues #41 and #1628.
#:
#: NOT THE SAME THING AS `JUDGED_NUMBERS` BELOW, which is for a sentence stating
#: no number at all, where somebody chose a magnitude. The skill lock's sentence
#: does state a number -- "2 seconds" -- and that number is its CONDITION value,
#: which `test_every_condition_value_appears_in_the_words_too` checks like any
#: other. Only the 1 is exempt, and only on these stats.
#:
#: `test_every_flag_stat_row_states_one` keeps this honest: a row on one of these
#: stats states 1 and nothing else, so the exemption cannot grow to cover a
#: magnitude nobody wrote down.
#:
#: `shield_absorbs_damage_over_time` JOINED ON 2026-09-23, issue #2014, with
#: "Energy shield can now be effected by bleed". Above zero it lets a bleed into
#: the wearer's energy shield; the sentence says what happens and the 1 says
#: that it happens.
FLAG_STATS = {"skill_locked", "mana_pool_becomes_health",
              "shield_absorbs_damage_over_time"}

#: Stats whose row carries 100 MINUS a number the sentence states, so the row
#: and the words say the same thing two ways round. The ruling of 2026-09-14 on
#: issue #1793, made by the coordinating session under the project owner's
#: delegation of that date.
#:
#: `healing_ceiling_reduction` IS WHAT THIS EXISTS FOR, and it is not in the
#: list yet. `UCataclysmRegeneration::TopUp` computes
#: `Ceiling *= (100 - Reduction) / 100`, so "You cannot heal above 60% of your
#: maximum HP" is a row carrying 40. The sentence says 60 and the row must say
#: 40, and `test_a_single_value_appears_in_its_words_outside_any_range` is right
#: to refuse a value that appears nowhere in its own words.
#:
#: THE SENTENCE IS THE CLEARER OF THE TWO AND STAYS AS IT IS. A player reads a
#: ceiling more easily than a reduction, and the stat stays a reduction for the
#: reasons `CataclysmVitalAttributeSet.h:183-189` gives: a stat holding the
#: ceiling itself would need 0 to mean "no cap", which reads as "cannot be
#: healed at all", and two such sources would sum in the flat bucket to 100 and
#: thereby REMOVE the cap. So neither the words nor the stat move, and the check
#: learns the relationship between them instead.
#:
#: EMPTY UNTIL ITS ROWS EXIST, which is the whole reason
#: `test_the_complement_list_holds_what_it_is_measured_to_hold` is below. The
#: two rows that fill it -- "You cannot heal above 60% of your maximum HP" and
#: "You cannot be healed above 75% of your maximum HP" -- are authored in the
#: design workbook, and a name added here before them would be an exemption with
#: nothing to excuse.
#:
#: FILLED ON 2026-09-14, with the two rows. "You cannot heal above 60% of your
#: maximum HP" is `healing_ceiling_reduction` flat 40 and "You cannot be healed
#: above 75%" is flat 25, and each sentence states the complement of its row.
COMPLEMENT_STATS: set[str] = {"healing_ceiling_reduction"}

#: Words that state one stat's value without a number, per stat. Issue #1815.
#:
#: "IMMUNE" IS 100 ON `crowd_control_resistance` AND ON NOTHING ELSE. Ruled
#: 2026-09-17 under the project owner's delegation: "While below 30% HP you are
#: immune to crowd control" is that stat, flat 100, because at 100
#: `UCataclysmSkillEffects::AfterCrowdControlResistance` lets no stun, knockdown
#: or shove land -- the one place the owner let a stat reach immunity, on
#: 2026-09-05. The knockdown joined the three on 2026-09-17, which is what the
#: row waited for.
#:
#: PER STAT, NOT A SECOND `MULTIPLYING_WORDS`, because the same word means a
#: different number, or none, on another stat. The sentence also states a number,
#: 30, which is its condition value, so `JUDGED_NUMBERS` cannot excuse it: that
#: list is for sentences that state no number at all.
#:
#: "ALL" IS 100 ON `armor_penetration`, ruled 2026-09-23 under the owner's
#: delegation for "Your first hit against each enemy ignores all armor". The
#: engine clamps armour penetration at 100 (`CataclysmDamageCalculation.cpp`),
#: so 100 is all of it. The same reasoning as "immune" above, on one stat.
STATED_BY_WORD: dict[str, dict[str, float]] = {
    "crowd_control_resistance": {"immune": 100.0},
    "armor_penetration": {"all": 100.0},
}

#: Enchantments whose sentence states no number, so the number was chosen under
#: the project owner's delegation of 2026-09-11 and recorded as a labelled
#: judgement in docs/DECISIONS.md. Each is excused from the two checks that need
#: the number, or an increase word, in the sentence, and from nothing else.
#: `test_every_judged_number_is_still_needed` keeps this list honest.
JUDGED_NUMBERS = {
    # "Your retaliation damage scales with your current HP percentage -- the
    # lower your HP the higher the retaliation": 1% increased retaliation
    # damage for every 2% of maximum health missing.
    "Positive_Your_retaliation_damage_scales_with_your_current",
    # "Skills that cost HP restore that amount as mana": the whole of the
    # health the cost took, returned as mana, so 100 of the amount the event
    # carried. "that amount" is all of it.
    "Positive_Skills_that_cost_HP_restore_that_amount_as_mana",
}

#: How many rows are written, and over how many enchantments. Pinned so that
#: the coverage only moves when somebody means it to, and says so in
#: `docs/DECISIONS.md` at the same time.
#: Seven rows over seven enchantments until 2026-09-11, when the ranged
#: enchantments' rows were written, and 64 over 56 until the four buildable
#: sets got their eight rows, and 81 over 73 until Brute's Heart and Demon
#: King's Regalia got six rows between them on 2026-09-12, and 87 over 77
#: until the two staggered damage enchantments got three rows between them
#: later the same day. Issue #45.
#:
#: THREE ROWS FOR TWO ENCHANTMENTS, WHICH IS THE SHEET'S PATTERN AND NOT A
#: MISCOUNT. "Staggered enemies take 20%-35% increased damage from all sources"
#: is the wearer's own damage across its types, so it is an `attack_damage` row
#: and a `spell_damage` row; "Staggered enemies deal 15%-30% increased damage to
#: you" is damage the wearer takes, which is `damage_taken` alone.
#:
#: 91 OVER 80 SINCE THE SKILL LOCK GOT ITS FIRST SOURCE, the same
#: day: "You cannot use movement abilities while stationary for more than 2
#: seconds" is the first row in this file to take a skill away rather than change
#: a number. Issues #41 and #1628.
#:
#: AND 93 OVER 82 SINCE THE TWO STAGGER STATS A STAGGERING CHARACTER CARRIES,
#: later the same day: "Stagger effects you apply last 50%-100% longer" and "You
#: cannot stagger enemies above 50% HP". ONE ROW EACH, so both numbers move by
#: two -- unlike the three-rows-for-two-enchantments entry above, where one
#: enchantment needed a row per damage type. Issue #45.
#:
#: AND 103 OVER 88 SINCE THE FIRST SIX DAMAGE-CHANGE ROWS THAT NEEDED NO NEW
#: MECHANISM, issue #1686 group A. SIX ENCHANTMENTS AND TEN ROWS: a row about
#: the damage a character's own skills deal becomes one row on `attack_damage`
#: and one on `spell_damage`, which is what the built "Melee skills deal
#: 20%-40% increased damage" does. The two about damage TAKEN are one row each.
#: AND 107 OVER 90 SINCE THE TWO ROWS THAT NEED A TAG AND A CONDITION AT ONCE,
#: issue #1686: "Spells deal 20%-35% less damage while you are moving" and
#: "Ranged skills deal 15%-30% less damage at close range (within 5 meters)".
#: TWO ENCHANTMENTS AND FOUR ROWS, the same doubling as the entry above.
#:
#: THE SECOND SENTENCE GAINED ITS DISTANCE SO THIS FILE COULD KEEP REFUSING A
#: HIDDEN NUMBER. `test_every_condition_value_appears_in_the_words_too` rejected
#: the row while it said only "at close range", and it was right to:
#: `target_within_metres` acts on a number the player could not read. The
#: sentence was changed rather than this file. `docs/DECISIONS.md` carries why
#: five metres, and why an appended clause is safe when a reworded opening is
#: not.
#: AND 108 OVER 91 SINCE THE MOVEMENT SPEED A FULL CLASS RESOURCE GRANTS, issue
#: #1686: "When your class resource is full, your movement speed is increased by
#: 15%-30%". ONE ROW OVER ONE ENCHANTMENT, so both numbers move by one -- unlike
#: the entry above, where four rows covered two enchantments.
#:
#: ITS PRECEDENT IS ONE ROW UP IN THE SAME TABLE. The critical strike sibling,
#: "When your class resource is full, critical strike chance is increased by
#: 15%-30%", is built on the same condition and states no duration either. The
#: third sibling says "for 3 seconds" and stays unwritten, because a duration
#: needs a condition the pipeline has not got.
#: AND 109 OVER 92 SINCE THE ULTIMATE LOCK, issue #1754: "Your ultimate ability
#: cannot be used unless you are below 50% HP". ONE ROW OVER ONE ENCHANTMENT, so
#: both numbers move by one -- the enchantment had no effect row at all before
#: this, which is what makes it newly authored as well as newly rowed.
#:
#: IT IS THE FIRST ROW IN THE GAME TO USE `health_at_or_above`. Issue #1653 added
#: that predicate for this exact sentence and closed, leaving the row unwritten
#: and `docs/DECISIONS.md` still saying the predicate did not exist. Nothing used
#: it for the whole time in between.
#:
#: THE SHAPE IS COPIED FROM THE MOVEMENT LOCK and not invented: same `skill_locked`
#: stat, same `flat` 1, a slot tag in place of a slot tag and a condition in place
#: of a condition. The only fields that differ are `Slot.Ultimate` for
#: `Slot.Movement` and `health_at_or_above` 50 for `stationary_for_seconds` 2.
#: AND 144 OVER 121 SINCE THE ROWS THAT ISSUE #1642'S SURVEY FOUND WRITABLE,
#: issue #45. FIFTEEN ENCHANTMENTS AND TWENTY-ONE ROWS: six of the fifteen say
#: "your damage", which is one `attack_damage` row and one `spell_damage` row
#: each, and the other nine are one row apiece.
#:
#: THE SURVEY OFFERED THIRTY-THREE AND SEVENTEEN OF THEM COULD NOT BE WRITTEN.
#: Eleven were proposed as `more` at -100 to mean "you have no armour", "cannot
#: block", "can't regen your hp" and the like.
#: `UCataclysmStatPipeline::LessMultiplierFloor` is -99 and `Accumulate` clamps
#: to it, so such a row leaves the wearer a hundredth of the stat rather than
#: none of it. A sentence stating a total removal is a rule and not a
#: magnitude, which this project already writes as a flag stat of 1 --
#: `FLAG_STATS` above, and the seven nodes at
#: `test_passive_effects_match_the_node_text.py`. Each of the eleven needs its
#: own flag stat and the C++ that reads it. Issue #1791.
#:
#: TWO MORE WERE REFUSED BY THE TWO CHECKS MEETING. A minion row is read
#: through `IncreasesForStat`, which returns the increases alone, so only the
#: `increased` bucket reaches a minion at all; but "20%-50% less hp" and "50%
#: more damage" are worded as multipliers, and
#: `test_an_increased_row_is_worded_as_an_increase` refuses an `increased` row
#: whose sentence says neither increased nor reduced. Issue #1792. Two further
#: rows fell with them because a set is written whole or not at all.
#:
#: AND TWO STATE A NUMBER THEIR ROW CANNOT CARRY. `healing_ceiling_reduction`
#: is the number taken off 100, so "you cannot heal above 60%" is a 40 that its
#: own sentence never says, and
#: `test_a_single_value_appears_in_its_words_outside_any_range` is right to
#: refuse it. Issue #1793.
#: AND 148 OVER 125 SINCE THE ROWS THE RULINGS OF 2026-09-14 UNBLOCKED, issues
#: #1792 and #1793. FOUR ENCHANTMENTS AND FOUR ROWS, one apiece.
#:
#: TWO ARE MINION ROWS AND BOTH ARE `increased`, WHICH IS NOT A STYLE CHOICE. A
#: minion reads its summoner's `minion_health` and `minion_damage` through
#: `IncreasesForStat`, which returns the increases alone, so a `more` row and a
#: `flat` row on those two stats are computed and then discarded. Set 6 lands
#: whole on them: its drawback and its first bonus.
#:
#: A FIFTH ROW WAS RULED AND IS NOT HERE. "Your minions have 20%-50% less hp"
#: needed its sentence reworded to reach the `increased` bucket, and the whole
#: sentence is 33 characters, so the reword moves the row name a dropped item
#: stores and orphans every saved item carrying it. Issue #1799 carries that to
#: the project owner. The sibling reword, Tyrant's Chains, was safe because its
#: changed word sits past the 48-character cap, and it is written.
#: AND 169 OVER 141 SINCE THE EIGHT ROWS THAT MOVE A POOL WHEN AN EVENT
#: HAPPENS, from 161 over 133, issue #1815. ONE ROW EACH:
#: a row moves one pool, where the damage rows above need one for attack and
#: one for spell. Seven fire on a block, a dodge or a class resource reaching
#: zero; the eighth returns the health a skill's cost took as mana and states
#: no number, which is why its enchantment is in JUDGED_NUMBERS.
#: AND 176 OVER 148 SINCE THE SEVEN ROWS ON A KILL, A CRITICAL STRIKE,
#: A DEATH NEARBY, A SKILL USE OR A STRIKE'S HIT, from 169 over
#: 141, issue #1815. ONE ROW EACH, because a row moves one pool.
#: AND 201 OVER 160 SINCE THE ELEVEN SENTENCES THAT REMOVE A STAT AND
#: STARVATION'S 2-PIECE BONUS, from 176 over 148, issue
#: #1791. "You have no resistances." is eight rows, one per resistance.
#: AND 204 OVER 162 SINCE "You cannot evade or block melee attacks" AND
#: "Cannot evade melee attacks", from 201 over 160, issue #1815. The
#: first is two rows, one removal each for evasion and block chance.
#: AND 205 OVER 163 SINCE "While below 30% HP you are immune to crowd
#: control", from 204 over 162, issue #1815. ONE FLAT ROW: 100
#: crowd control resistance under health_below 30, which is immunity because at
#: 100 AfterCrowdControlResistance lets nothing land. It was held until a
#: knockdown read that stat, in #1954.
#: AND 211 OVER 169 SINCE THE SIX MANA COST SENTENCES, from 205 over 163,
#: issue #1815. ONE ROW EACH on `mana_cost`, whose base is not a figure the
#: character holds but the cost the skill itself states: two that reduce a cost,
#: three that raise one, and one removal under `health_below` 50. The stat and
#: its four readers were built in #1962; these are its first enchantment rows.
#: AND 257 OVER 198 SINCE THE THIRTEEN ROWS ACROSS TEN SENTENCES that issues
#: #1981 and #1982 unblocked, from 244 over 188. #1988 WAS NAMED HERE TOO AND
#: DOES NOT BELONG: it asks for a check that every scale source is named by a
#: row or listed as landed ahead of it, and these rows add no check.
#: NO SENTENCE WAS ADDED
#: TO THE ENCHANTMENTS SHEET: the second number counts enchantments that have
#: at least one effect row, and these ten already existed with none, so it
#: moves by ten while the sheet itself gains nothing.
#: Three shorten a cooldown as flat rows, because an increase scales a base and
#: cooldown reduction has none; three sentences take two rows each, one for
#: attack damage and one for spell damage; the rest are one row each.
#: AND 263 OVER 204 SINCE THE SIX COOLDOWN ROWS OF ISSUE #1994,
#: from 257 over 198: one row each on six enchantments that had
#: none. The Boss window row is a flat `cooldown_reduction` under
#: `seconds_after_striking_a_boss` 4, its sentence lengthened after its first
#: 48 characters so its row name holds; the five drawbacks that make a
#: cooldown longer are flat `cooldown_lengthening` rows carrying their
#: sentences' own numbers.
#: AND 264 OVER 205 SINCE ISSUE #2014, from 263 over 204: one flag row on
#: "Energy shield can now be effected by bleed", an enchantment that had none.
#: It grants `shield_absorbs_damage_over_time`, which since that issue lets a
#: bleed into the wearer's energy shield.
#: AND 270 OVER 211 SINCE GROUP B OF ISSUE #1815, from 264
#: over 205: one row each on 6 enchantments that had none -- the five movement speed windows' rows and the one lengthened to three seconds.
#: AND 274 OVER 214 SINCE THE FIRST HIT AGAINST EACH ENEMY, issue #1815, from
#: 270 over 211: four rows on three enchantments that had none. "Deals
#: 100%-300% bonus damage" is an attack damage row and a spell damage row;
#: "ignores all armor" and the first critical strike are one row each.
#: AND 280 OVER 219 SINCE THE COMBAT STATE, issue #1815, from 274 over 214: six
#: rows on five enchantments that had none. "Your damage is reduced ... out of
#: combat" is an attack damage row and a spell damage row; the rest are one row
#: each. One sentence was lengthened after its first 48 characters.
#: AND 293 OVER 227 SINCE ISSUE #1815'S SECTION C, from 280 over 219: thirteen
#: rows on eight enchantments that had none. Five sentences are about the
#: wearer's damage and take an attack damage row and a spell damage row each;
#: the low mana drawback, the critical strike chance and the Spells-only buff
#: row are one row each.
#: AND 294 OVER 228 SINCE THE MINION DRAWBACK, issue #1815, from 293 over 227:
#: "Each active minion reduces your maximum HP by 3%-6%", one row.
#: AND 298 OVER 231 SINCE ISSUE #1686'S FIRST WINDOW, from 294 over 228: four
#: rows on three enchantments that had none. "Point blank AOE skills deal
#: 15%-25% less damage to a single target" is an attack damage row and a spell
#: damage row; the aura and crowd control drawbacks are one row each.
#: AND 303 OVER 234 SINCE THE CLASS POINT ROWS, issue #1686,
#: from 298 over 231: five rows on three enchantments that had none.
#: The two damage sentences take an attack damage row and a spell damage
#: row each; the maximum health drawback is one row.
#: AND 304 OVER 235 SINCE THE NON-CRITICAL DRAWBACK,
#: issue #1686, from 303 over 234: one row on one enchantment.
#: AND 305 OVER 236 SINCE THE PROJECTILE LATER-HIT DRAWBACK,
#: issue #1686, from 304 over 235: one row on one enchantment.
AUTHORED_ROWS = 305
AUTHORED_ENCHANTMENTS = 236

#: How many rows remove their stat, measured with the 201 above. Issue #1791.
#: Without it `test_a_removed_row_is_worded_as_a_removal` and
#: `test_every_removed_row_states_one` pass on a table holding no removal at
#: all, which is what a table built before the rows existed looks like.
#: AND 25 SINCE THE THREE MELEE REMOVALS, from 22, issue #1815: the first
#: removals with a condition, which take the stat away from a melee blow alone.
#: AND 26 SINCE "While below 50% HP your skills cost no mana", from 25, issue
#: #1815: the first removal on the enchantment side whose base is a figure the
#: caller supplies rather than one the character holds. Ritual Focus, the
#: passive node added in #1966, is the same shape on the passive side.
#: AND 32 SINCE "HP regeneration is disabled during combat", from 31, issue
#: #1815: a removal under `in_combat`.
REMOVED_ROWS = 32

#: The named sets whose rows are written, by the identifier their Weight column
#: carries: Archon's Aegis (5), Mana Weaver (8), Brute's Heart (9), Demon King's
#: Regalia (11), Divine Retribution (16) and Warlord's Will (17). The other
#: eight wait for what their rows need, and `docs/DECISIONS.md` says what each
#: one waits for.
#:
#: SIX OF FOURTEEN SINCE 2026-09-12, and four before that. Brute's Heart and
#: Demon King's Regalia were both waiting for one reading: an attacker asking
#: how far away its target is, which is `target_within_metres`. Their 2-piece
#: bonuses differ by one word -- one says "increased" and the other "more" --
#: which is why that reading is a condition on a row rather than a value added
#: in code. Their drawbacks needed nothing new and are written with them,
#: because a set is written whole or not at all.
#:
#: SEVEN OF FOURTEEN SINCE 2026-09-13. Plague Doctor (12) needed nothing new:
#: its 2-piece bonus is one `dot_damage` row and its drawback two rows on the
#: two direct damage stats. Its 6-piece and 10-piece rows still wait, and a set
#: counts as working here once any of its rows is written.
#:
#: EIGHT OF FOURTEEN SINCE 2026-09-14. Tyrant's Chains (6) needed one ruling and
#: no new mechanism: its first bonus and its drawback are both minion rows in
#: the `increased` bucket.
#:
#: NINE OF FOURTEEN SINCE 2026-09-16. Starvation (13) needed the removal of
#: issue #1791: its drawback, "You have no health/mana/es regen", is three
#: removed rows, and its 2-piece bonus three flat leech rows. Its 6-piece and
#: 10-piece rows still wait.
SETS_THAT_WORK = [5, 6, 8, 9, 11, 12, 13, 16, 17]

#: How many ranges the two enchantment tables state, measured on 2026-09-11
#: with a separate search of the two CSV files. The game's own reader,
#: `UCataclysmItemValues::EnchantmentRanges`, is held to the same number by
#: `Cataclysm.Enchantments.EveryRangeTheTablesStateIsFoundAndReplaced`, so the
#: generator and the game cannot read the sentences differently.
STATED_RANGES = 390


def read(path: pathlib.Path) -> list[dict]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def effects() -> list[dict]:
    if not EFFECTS_CSV.is_file():
        pytest.skip(f"{EFFECTS_CSV.name} has not been generated")
    rows = read(EFFECTS_CSV)
    assert rows, (f"{EFFECTS_CSV.name} is empty, so every check below would "
                  f"pass having read nothing")
    return rows


@pytest.fixture(scope="module")
def enchantments() -> dict[str, dict]:
    rows: dict[str, dict] = {}
    for path in ENCHANTMENT_CSVS:
        for row in read(path):
            rows[row["Name"]] = row
    assert len(rows) > 500, "the two enchantment tables did not load"
    return rows


def numbers_in(text: str) -> set[float]:
    """Every number in a sentence, with thousands commas read as such."""
    return {float(n.replace(",", ""))
            for n in re.findall(r"(?:\d[\d,]*\d|\d)(?:\.\d+)?", text)}


def outside_ranges(text: str) -> str:
    """The sentence with every range it states taken out."""
    return gen.ENCHANTMENT_RANGE.sub(" ", text)


def words_of(row: dict, enchantments: dict[str, dict]) -> str:
    return enchantments[row["Enchantment"]]["Effect"]


def takes_something_away(stat: str, words: str) -> bool:
    """Whether a negative value belongs on these words for this stat."""
    return bool(TAKING.search(words)) or (
        stat in LONGER_WHEN_NEGATIVE and bool(LONGER.search(words)))


def value_is_stated(stat: str, value: float, words: str,
                    complement_stats: set[str] | None = None,
                    stated_by_word: dict[str, dict[str, float]] | None = None
                    ) -> bool:
    """Whether a row's single value can be read out of its own sentence.

    FOUR WAYS IT CAN BE: the number itself is in the words; a multiplying word
    such as "doubled" means it; for a stat in `complement_stats`, the words
    state 100 minus it; or a word `stated_by_word` gives for this stat, such as
    "immune" on crowd_control_resistance, means it.

    THE EXEMPT SET IS A PARAMETER SO IT CAN BE EXERCISED WHILE THE REAL ONE IS
    EMPTY. `test_the_complement_exemption_reads_only_its_own_stats` passes a set
    of its own, which is the arrangement
    `test_longer_excuses_a_negative_value_on_one_stat_only` uses above and for
    the same reason: a check proved only against the real tables stops being
    proved the moment those tables change.
    """
    if complement_stats is None:
        complement_stats = COMPLEMENT_STATS
    if stated_by_word is None:
        stated_by_word = STATED_BY_WORD

    stated = numbers_in(outside_ranges(words))
    if abs(value) in stated:
        return True

    spelled = {word.lower() for word in re.findall(r"[A-Za-z]+", words)}
    if any(MULTIPLYING_WORDS.get(word) == value for word in spelled):
        return True

    # AND A WORD THAT MEANS A NUMBER ON THIS STAT ALONE.
    if any(stated_by_word.get(stat, {}).get(word) == value for word in spelled):
        return True

    # AND THE COMPLEMENT, FOR THE STATS THAT CARRY ONE. A row of 40 on a
    # sentence saying 60 is the same fact twice, not a number nobody wrote.
    return stat in complement_stats and (100.0 - abs(value)) in stated


def test_every_effect_names_an_enchantment_that_exists(effects, enchantments):
    missing = sorted(r["Name"] for r in effects
                     if r["Enchantment"] not in enchantments)
    assert not missing, (
        f"{missing} name enchantments neither table holds, so they would "
        f"apply to nothing. The generator refuses such a row, so the CSV is "
        f"older than the workbook.")


def set_rows(enchantments: dict[str, dict]):
    """Each set's bonus rows, sorted by threshold, and its drawback rows."""
    bonuses: dict[int, list[tuple[int, str]]] = {}
    drawbacks: dict[int, list[str]] = {}
    for name, row in enchantments.items():
        set_id = gen.enchantment_set_id(row)
        if not set_id:
            continue
        if row["IsNegative"] == "True":
            drawbacks.setdefault(set_id, []).append(name)
        else:
            threshold = gen.set_piece_threshold(row["Effect"])
            bonuses.setdefault(set_id, []).append(
                (gen.UNREACHABLE_THRESHOLD if threshold is None else threshold,
                 name))
    return ({set_id: sorted(rows) for set_id, rows in bonuses.items()},
            {set_id: sorted(rows) for set_id, rows in drawbacks.items()})


def test_every_set_with_an_effect_is_written_whole(effects, enchantments):
    """A set's first bonus and its drawback turn on together, at the same
    threshold, which the project owner ruled on 2026-09-08. So a set written by
    halves would be a bonus with no cost, or a cost with no bonus. The generator
    refuses that; this holds the file it wrote to the same rule."""
    written = {row["Enchantment"] for row in effects}
    bonuses, drawbacks = set_rows(enchantments)

    half = []
    for set_id in sorted(set(bonuses) | set(drawbacks)):
        ordered, named = bonuses.get(set_id, []), drawbacks.get(set_id, [])
        if not ({name for _, name in ordered} | set(named)) & written:
            continue
        if not ordered or ordered[0][1] not in written:
            half.append(f"set {set_id} has no effect on its first bonus")
        if not named or named[0] not in written:
            half.append(f"set {set_id} has no effect on its drawback")
    assert not half, "; ".join(half)


def test_the_sets_that_work_are_the_ones_counted_here(effects, enchantments):
    """Nine of the fourteen sets have a row written: Archon's Aegis (5),
    Tyrant's Chains (6), Mana Weaver (8), Brute's Heart (9), Demon King's
    Regalia (11), Plague Doctor (12), Starvation (13), Divine Retribution
    (16) and Warlord's Will (17). The other five wait
    for what their rows need, which `docs/DECISIONS.md` lists set by set. This
    moves only when somebody means it to.

    THIS SENTENCE SAID "FOUR" AND NAMED FOUR WHILE THE LIST BELOW HELD SIX.
    The count above it was kept current and this one was not, so read the
    constant rather than this paragraph if they ever disagree again."""
    written = {row["Enchantment"] for row in effects}
    bonuses, drawbacks = set_rows(enchantments)
    working = sorted(
        set_id for set_id in set(bonuses) | set(drawbacks)
        if ({name for _, name in bonuses.get(set_id, [])}
            | set(drawbacks.get(set_id, []))) & written)

    assert working == SETS_THAT_WORK


def test_a_range_is_one_the_words_state(effects, enchantments):
    """The game shows the item's number in place of the range in the sentence,
    so a row whose two ends are not that range, in that order, would give the
    character one number and the player another."""
    wrong = []
    for row in effects:
        low, high = float(row["ValueLow"]), float(row["ValueHigh"])
        if low == high:
            continue
        text = words_of(row, enchantments)
        if ((low < 0) != (high < 0)
                or (abs(low), abs(high)) not in gen.enchantment_ranges(text)):
            wrong.append(f"{row['Name']}: {low:g} to {high:g} against {text!r}")
    assert not wrong, "; ".join(wrong)


def test_a_single_value_appears_in_its_words_outside_any_range(effects,
                                                              enchantments):
    """A single value that is only an end of a range the sentence states is a
    range written as one number, and would ignore the roll."""
    wrong = []
    for row in effects:
        value = float(row["ValueLow"])
        if value != float(row["ValueHigh"]):
            continue
        if row["Enchantment"] in JUDGED_NUMBERS:
            continue
        if row["Stat"] in FLAG_STATS:
            continue
        # A REMOVAL STATES 1 AND NO SENTENCE SAYS IT, for the reason a flag
        # stat's does not. Issue #1791. `test_every_removed_row_states_one`
        # keeps the excuse from covering a magnitude, and
        # `test_a_removed_row_is_worded_as_a_removal` checks the words instead.
        if row["ValueKind"] == "removed":
            continue
        text = words_of(row, enchantments)
        if not value_is_stated(row["Stat"], value, text):
            wrong.append(f"{row['Name']}: {value:g} against {text!r}")
    assert not wrong, (
        "these values appear nowhere in their enchantment's words outside a "
        "range, as a number, as a multiplying word, as the complement of a "
        "number for a stat in COMPLEMENT_STATS, or as a word STATED_BY_WORD "
        "gives for the stat: " + "; ".join(wrong))


#: Words that state a CONDITION's value without a number, per condition. Issue
#: #1815. The sibling of `STATED_BY_WORD`, which is keyed by stat and covers a
#: row's value; this is keyed by condition and covers its condition value, and
#: `test_every_condition_value_appears_in_the_words_too` alone reads it.
#:
#: "LOW MANA" IS 35 ON `mana_below`, a labelled judgement tied to the ruling of
#: 2026-09-23 under the owner's delegation that low mana is below 35% of maximum
#: mana (Path of Exile 2's Low Mana is 35%, and this game's own Low Life keystone
#: is 35%). "Take 10%-40% more damage when on low mana" is 41 characters, so a
#: clause stating the number could not be appended without renaming its row.
#: `test_the_low_mana_row_is_refused_without_its_word` is the control.
#:
#: "SINGLE TARGET" IS 1 ON `enemies_hit_at_most`, ruled 2026-09-23 under the
#: owner's delegation for "Point blank AOE skills deal 15%-25% less damage to a
#: single target": exactly one enemy struck by the attack. Issue #1686.
CONDITION_VALUE_STATED_BY_WORD: dict[str, dict[str, float]] = {
    "mana_below": {"low mana": 35.0},
    "enemies_hit_at_most": {"single target": 1.0},
}


def condition_values_not_stated(effects, enchantments, word_map) -> list[str]:
    """The rows whose condition value is neither a number in their sentence nor
    stated by a phrase `word_map` gives their condition."""
    wrong = []
    for r in effects:
        value = float(r["ConditionValue"])
        if not value:
            continue
        words = words_of(r, enchantments)
        by_word = {amount for phrase, amount in word_map.get(r["Condition"], {}).items()
                   if re.search(rf"\b{re.escape(phrase)}\b", words, re.IGNORECASE)}
        if value not in numbers_in(words) | by_word:
            wrong.append(f"{r['Name']}: {r['ConditionValue']} against {words!r}")
    return wrong


def test_every_condition_value_appears_in_the_words_too(effects, enchantments):
    wrong = condition_values_not_stated(effects, enchantments,
                                        CONDITION_VALUE_STATED_BY_WORD)
    assert not wrong, "; ".join(wrong)


def test_the_low_mana_row_is_refused_without_its_word(effects, enchantments):
    """The control for the word map above: with it emptied, the low mana row is
    named, so the map is what lets it through and the check can still fail."""
    wrong = condition_values_not_stated(effects, enchantments, {})
    assert any(line.startswith("Negative_Take_10_40_more_damage_when_on_low_mana#1:")
               for line in wrong), wrong


def test_the_single_target_rows_are_refused_without_their_words(effects, enchantments):
    """The same control for "single target": with the map emptied, both point
    blank rows are named, so the map is what lets them through. Issue #1686."""
    wrong = condition_values_not_stated(effects, enchantments, {})
    for suffix in ("#1:", "#2:"):
        assert any(line.startswith(
            "Negative_Point_blank_AOE_skills_deal_15_25_less_damage" + suffix)
            for line in wrong), wrong


def offsets_not_stated(effects, enchantments) -> list[str]:
    """The rows whose Scale Offset is not a number in their own sentence."""
    wrong = []
    for r in effects:
        offset = float(r.get("ScaleOffset") or 0.0)
        if offset and offset not in numbers_in(words_of(r, enchantments)):
            wrong.append(f"{r['Name']}: {offset:g} against {words_of(r, enchantments)!r}")
    return wrong


def test_every_scale_offset_appears_in_the_words(effects, enchantments):
    """"Above 100" is an offset of 100. Issue #1686. An offset the sentence
    does not state is a number nobody wrote."""
    wrong = offsets_not_stated(effects, enchantments)
    assert not wrong, "; ".join(wrong)


def test_the_offset_check_can_fail():
    """The control: a row whose offset its words do not state is named."""
    effects = [{"Name": "A#1", "Enchantment": "A", "ScaleOffset": "100.0"}]
    enchantments = {"A": {"Effect": "for every 10 class points spent above 50"}}
    assert offsets_not_stated(effects, enchantments) == [
        "A#1: 100 against 'for every 10 class points spent above 50'"]


def test_a_more_row_is_worded_as_a_multiplier(effects, enchantments):
    wrong = [f"{r['Name']}: {words_of(r, enchantments)!r}"
             for r in effects
             if r["ValueKind"] == "more"
             and not MULTIPLIER.search(words_of(r, enchantments))]
    assert not wrong, (
        "these rows are in the more bucket and their sentence does not say "
        "more, less, double or the like: " + "; ".join(wrong))


def test_an_increased_row_is_worded_as_an_increase(effects, enchantments):
    wrong = [f"{r['Name']}: {words_of(r, enchantments)!r}"
             for r in effects
             if r["ValueKind"] == "increased"
             and r["Enchantment"] not in JUDGED_NUMBERS
             and not INCREASE.search(words_of(r, enchantments))]
    assert not wrong, (
        "these rows are in the increased bucket and their sentence does not "
        "say increased, reduced or the like: " + "; ".join(wrong))


def test_a_removed_row_is_worded_as_a_removal(effects, enchantments):
    """A row that takes its stat to nothing is on a sentence saying the stat is
    gone. Issue #1791. The words are all there is to check a removal against,
    because it states no number."""
    wrong = [f"{r['Name']}: {words_of(r, enchantments)!r}"
             for r in effects
             if r["ValueKind"] == "removed"
             and not REMOVING.search(words_of(r, enchantments))]
    assert not wrong, (
        "these rows remove their stat and their sentence does not say no, "
        "cannot, can't or zero: " + "; ".join(wrong))


def test_every_removed_row_states_one(effects):
    """A removal carries no amount, so its row states 1 and nothing else. That
    is what excuses it from the check that a value appears in its words, and
    this keeps the excuse from covering a number somebody meant. The argument
    `test_every_flag_stat_row_states_one` makes, for a kind rather than a stat."""
    wrong = [f"{r['Name']}: {r['ValueLow']} to {r['ValueHigh']}"
             for r in effects
             if r["ValueKind"] == "removed"
             and (float(r["ValueLow"]) != 1.0
                  or float(r["ValueHigh"]) != 1.0)]
    assert not wrong, (
        "these rows remove their stat and state something other than 1, so "
        "they are excused the value-in-words check while carrying a "
        "magnitude: " + "; ".join(wrong))


def test_a_removal_is_read_from_whole_words_only():
    """The removal words, checked on made-up sentences for the reason
    `test_a_sentence_that_drains_takes_something_away` gives: a rule checked
    only against the real tables stops being checked when they change.

    THE LAST FOUR ARE THE CONTROL. Three hold "no" or "zero" inside a longer
    word, and one says "can" without the negation; none says a stat is gone."""
    for words in ("You have no armor", "You no longer regenerate mana",
                  "Cannot block", "Can't regen your hp",
                  "Your maximum mana is reduced to zero"):
        assert REMOVING.search(words), words
    for words in ("Nobody escapes your armor", "Your cannons fire twice",
                  "You can block twice as often",
                  "Zeroes in on the weakest enemy"):
        assert not REMOVING.search(words), words


def test_a_negative_value_is_on_words_that_take_something_away(effects,
                                                              enchantments):
    wrong = [f"{r['Name']}: {words_of(r, enchantments)!r}"
             for r in effects
             if float(r["ValueLow"]) < 0
             and not takes_something_away(r["Stat"], words_of(r, enchantments))]
    assert not wrong, "; ".join(wrong)


def test_the_complement_exemption_reads_only_its_own_stats():
    """The complement check, on made-up rows, so that no change to the real
    tables can hide a mistake in it and so that it is exercised at all while
    COMPLEMENT_STATS is empty. The same arrangement, and the same argument, as
    `test_longer_excuses_a_negative_value_on_one_stat_only` below."""
    exempt = {"healing_ceiling_reduction"}
    ceiling = "You cannot heal above 60% of your maximum HP"

    # THE CASE IT EXISTS FOR: the row says 40 and the words say 60.
    assert value_is_stated("healing_ceiling_reduction", 40.0, ceiling, exempt)
    assert value_is_stated(
        "healing_ceiling_reduction", 25.0,
        "You cannot be healed above 75% of your maximum HP", exempt)

    # AND IT IS THE COMPLEMENT AND NOT ANY NUMBER. A row of 40 against a
    # sentence saying 70 is a mismatch and stays one.
    assert not value_is_stated(
        "healing_ceiling_reduction", 40.0,
        "You cannot heal above 70% of your maximum HP", exempt)

    # AND IT REACHES ONLY THE STATS NAMED. The same row on another stat is
    # refused, so the exemption cannot leak across the sheet.
    assert not value_is_stated("max_health", 40.0, ceiling, exempt)

    # AND AN EMPTY SET CHANGES NOTHING ABOUT THE ORDINARY TWO WAYS. This is what
    # holds while the real list is empty: the check still accepts a number in
    # the words and a multiplying word, and still refuses everything else.
    assert value_is_stated("max_health", 50.0, "Your health is reduced by 50%", set())
    assert value_is_stated("max_energy_shield", 100.0, "Double your energy shield", set())
    assert not value_is_stated("healing_ceiling_reduction", 40.0, ceiling, set())


def test_a_word_states_a_value_only_on_its_own_stat():
    """The word rule, on made-up sentences, for the reason
    `test_the_complement_exemption_reads_only_its_own_stats` gives. Issue #1815.

    THE FIRST ASSERTION IS THE CASE IT EXISTS FOR; the rest are the control. The
    word means its number and no other, on its stat and no other, and only when
    the sentence says the word."""
    words = {"crowd_control_resistance": {"immune": 100.0}}
    sentence = "While below 30% HP you are immune to crowd control"

    assert value_is_stated("crowd_control_resistance", 100.0, sentence,
                           set(), words)
    assert not value_is_stated("crowd_control_resistance", 50.0, sentence,
                               set(), words)
    assert not value_is_stated("max_health", 100.0, sentence, set(), words)
    assert not value_is_stated(
        "crowd_control_resistance", 100.0,
        "While below 30% HP you resist crowd control", set(), words)
    # AND THE REAL TABLE IS WHAT THE TEST ABOVE READS BY DEFAULT.
    assert value_is_stated("crowd_control_resistance", 100.0, sentence)


def test_every_word_stated_value_is_still_needed(effects, enchantments):
    """An excuse that outlives its reason hides a real mismatch, the argument
    `test_every_judged_number_is_still_needed` makes. Each word in
    STATED_BY_WORD must still be what admits a row: a single value on its stat,
    on a sentence saying the word, that the other three ways cannot read out of
    the words. Issue #1815.

    PER WORD, NOT PER STAT. `crowd_control_resistance` had a row before the word
    existed, "CC effects applied to you last 40%-70% longer", so a stat that
    still has a row says nothing about whether its word is still needed."""
    for stat, meanings in sorted(STATED_BY_WORD.items()):
        for word, value in sorted(meanings.items()):
            needing = [
                r["Name"] for r in effects
                if r["Stat"] == stat
                and float(r["ValueLow"]) == value == float(r["ValueHigh"])
                and word in {w.lower() for w in
                             re.findall(r"[A-Za-z]+", words_of(r, enchantments))}
                and not value_is_stated(stat, value, words_of(r, enchantments),
                                        None, {})]
            assert needing, (
                f"no row needs {word!r} to state {value:g} on {stat}, so the "
                f"word should leave STATED_BY_WORD")


def test_every_complement_stat_is_still_used(effects):
    """An exemption that outlives its reason hides a real mismatch, which is the
    argument `test_every_flag_stat_is_still_used` and
    `test_every_judged_number_is_still_needed` both make."""
    written = {r["Stat"] for r in effects}
    unused = sorted(COMPLEMENT_STATS - written)
    assert not unused, (
        f"{unused} are excused the value-in-words check by way of their "
        f"complement and no row grants them")


def test_the_complement_list_holds_what_it_is_measured_to_hold():
    """WITHOUT THIS THE TWO TESTS ABOVE CANNOT FAIL. Both loop over
    COMPLEMENT_STATS, and it is empty, so both pass having checked nothing --
    which is the "guard over an empty list" this project already warns about in
    `CataclysmPassiveTreeTests.cpp`. This says what the list holds today, so
    filling it is a deliberate edit here rather than a silent one.

    IT HELD NOTHING UNTIL 2026-09-14, and this assertion is what made that
    honest rather than vacuous. `healing_ceiling_reduction` went in with the two
    rows that need it, in one commit, so the exemption and its users have never
    existed apart. Issue #1793."""
    assert COMPLEMENT_STATS == {"healing_ceiling_reduction"}, (
        f"COMPLEMENT_STATS holds {sorted(COMPLEMENT_STATS)}. If that is "
        f"deliberate, change this test and check that every name in it has a "
        f"row in game/Data/EnchantmentEffects.csv.")


def test_longer_excuses_a_negative_value_on_one_stat_only():
    """The one widening this file allows, checked on made-up rows so that no
    change to the real tables can hide a mistake in it. A negative
    crowd_control_resistance makes crowd control last longer; a negative
    value beside "longer" on a duration is a sign error and stays refused."""
    assert takes_something_away(
        "crowd_control_resistance", "CC effects applied to you last 40%-70% longer")
    assert not takes_something_away(
        "debuff_duration_taken", "Debuffs on you last 20%-30% longer")
    assert not takes_something_away(
        "crowd_control_resistance", "CC effects applied to you are 40%-70% stronger")


def test_a_sentence_that_drains_takes_something_away():
    """The word added on 2026-09-14, checked on made-up sentences for the
    reason the test above gives: a rule checked only against the real tables
    stops being checked the moment those tables change.

    ALL THREE TENSES, because the sentences use two of them and a reword could
    use the third. The last two assertions are the control: the word has to be
    what admits the sentence, rather than something else in it, and it has to
    be the whole word."""
    assert takes_something_away(
        "class_resource",
        "Dodging an attack drains 5%-10% of your class resource")
    assert takes_something_away(
        "health", "Channel skills drain 8%-15% of your maximum HP per second")
    assert takes_something_away(
        "class_resource",
        "Your class resource is drained by 5%-10% when you dodge")
    assert not takes_something_away(
        "class_resource",
        "Dodging an attack costs 5%-10% of your class resource")
    assert not takes_something_away(
        "class_resource", "Dodging an attack blocks your class drainpipe")


def test_the_tables_state_the_measured_number_of_ranges(enchantments):
    counted = sum(len(gen.enchantment_ranges(row["Effect"]))
                  for row in enchantments.values())
    assert counted == STATED_RANGES, (
        f"the generator reads {counted} ranges in the two enchantment tables, "
        f"and {STATED_RANGES} were measured. Either the sentences changed, or "
        f"the reader in tools/generate_datatables.py no longer reads what "
        f"UCataclysmItemValues::EnchantmentRanges reads. Change this number "
        f"and the automation test that pins the same count together.")


def test_the_coverage_is_what_it_is_measured_to_be(effects):
    assert len(effects) == AUTHORED_ROWS, (
        f"{len(effects)} effect rows, pinned at {AUTHORED_ROWS}. Change the "
        f"pin and the entry in docs/DECISIONS.md that states it together.")
    assert len({r["Enchantment"] for r in effects}) == AUTHORED_ENCHANTMENTS


def test_the_removed_rows_are_the_ones_counted_here(effects):
    """The two removal checks above loop over the rows that remove a stat, so
    a table with none passes both. This is what makes them read something.
    Issue #1791."""
    removed = sum(1 for r in effects if r["ValueKind"] == "removed")
    assert removed == REMOVED_ROWS, (
        f"{removed} rows remove their stat, pinned at {REMOVED_ROWS}. Change "
        f"the pin with the rows.")


def test_every_flag_stat_row_states_one(effects):
    """A stat whose value is a yes states 1, so the exemption above cannot come
    to cover a magnitude the sentence does not mention."""
    wrong = [f"{r['Name']}: {r['ValueLow']} to {r['ValueHigh']}"
             for r in effects
             if r["Stat"] in FLAG_STATS
             and (float(r["ValueLow"]) != 1.0
                  or float(r["ValueHigh"]) != 1.0)]
    assert not wrong, (
        "these rows are on an on-or-off stat and state something other than 1, "
        "so they are excused the check that a value appears in its words while "
        "carrying a magnitude: " + "; ".join(wrong))


def test_every_flag_stat_is_still_used(effects):
    """An exemption that outlives its reason hides a real mismatch, which is the
    argument `test_every_judged_number_is_still_needed` makes below."""
    written = {r["Stat"] for r in effects}
    unused = sorted(FLAG_STATS - written)
    assert not unused, (
        f"{unused} are excused the value-in-words check and no row grants them")


def test_every_judged_number_is_still_needed(effects, enchantments):
    """An excuse that outlives its reason hides a real mismatch. Each name in
    JUDGED_NUMBERS must still have a row, and its sentence must still state no
    number; once the words gain one, the row is checked like every other."""
    written = {r["Enchantment"] for r in effects}
    for name in sorted(JUDGED_NUMBERS):
        assert name in written, f"{name} is excused but has no effect row"
        assert not numbers_in(enchantments[name]["Effect"]), (
            f"{name}'s words now state a number, so it needs no excuse: "
            f"{enchantments[name]['Effect']!r}")
