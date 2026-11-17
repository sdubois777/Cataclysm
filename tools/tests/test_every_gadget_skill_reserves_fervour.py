"""Every skill that makes a minion or a gadget reserves Fervour.

WHY THIS EXISTS. The project owner ruled on 2026-09-07 that every minion and
gadget skill reserves Fervour, not only the Staff Ultimate that takes thralls,
and reaffirmed it on 2026-09-09 alongside the ruling that the Ultimate slot costs
50 Fervour. Both are in `docs/DECISIONS.md`. **Nothing checked it.** The rule was
applied by hand to the five skills that existed at the time, and the next skill
built could have missed it in silence.

WHAT SILENCE LOOKS LIKE HERE. A deployable with no `FervourReserve` is free. It
costs its mana and nothing else, so a character can hold as many as the skill's
own `MaxActive` allows and the Fervour pool never notices. The design's rule that
"the army cap is the pool itself" simply stops being true for that one skill, and
nothing at run time reports it -- the turret works, the numbers are all sensible,
and the only sign is that a player can have more of them than they should.

THE SHAPE COLUMN IS THE RIGHT QUESTION TO ASK, and it took a wrong answer to see
why. On 2026-09-09 the question "is there a coverage gap?" was first answered by
listing skills whose names read like gadgets and observing that they had a shape
of Debuff, Projectile, Movement or blank, and concluding none was a gadget. **A
blank Shape is not evidence that a skill is not a gadget.** It means the skill
has no behaviour yet: 340 of the 403 rows of `game/Data/WeaponSkills.csv` have a
blank Shape and every one of them has empty `ShapeParams`.

So this file checks the rule against the skills that ARE built, which is the only
population where the rule can be broken, and records the unbuilt ones below so
that whoever builds them finds the rule rather than rediscovering it.

WHY `Summon` AND `Deployable` AND NO OTHER SHAPE. `SHAPE_PARAMS` in
`tools/generate_datatables.py` allows `FervourReserve` on exactly those two and
refuses it everywhere else, so they are the two shapes that can carry a
reservation at all. A gadget built under any other shape would be refused by the
generator before it reached this check.

WHAT THIS DOES NOT CHECK, and it is the larger half. It cannot know whether a
skill that SHOULD be a summon or a deployable was built as something else. Ten
unbuilt skills describe planting something that persists and acts on its own, and
if one of them is built as a `Projectile` that happens to spawn a mine, this file
will not notice:

    Gut Wire            War_Dagger_Heavy        a tripwire that arms
    Impaler             War_Spear_Heavy         a spike trap
    Grapple             War_Crossbow_Movement   leaves a trip mine
    Vanishing Trap      War_Dagger_Movement     leaves a blade trap
    Bolt Trap           War_2H_Crossbow_Special a trap that fires
    Proximity Mine      War_Dagger_Special      a mine
    Shield Wall         War_Shield_Special      a barrier that lasts 8 seconds
    Snare Line          War_Whip_Special        a whip trap
    Artillery Barrage   War_Crossbow_Ultimate   deploys a mine per bolt
    Death by a Thousand War_Dagger_Ultimate     scatters 5 mines
      Cuts

Two more carry a gadget tag and correctly reserve nothing, because they act on
gadgets rather than making them: `Cavalry Charge` triggers traps already on the
ground, and `Phalanx Stance` buffs gadgets already out. Three built skills carry
`Keyword.Summon` and correctly reserve nothing for the same reason -- `Quarry`,
`Compel` and `Vesselstep` command minions that already exist.

Which of those counts as a gadget is a judgement, and `docs/DECISIONS.md` records
it as one rather than as a measurement.
"""

from __future__ import annotations

import csv
import pathlib

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SKILLS_CSV = REPO_ROOT / "game" / "Data" / "WeaponSkills.csv"

#: The two shapes that make something which persists in the world under the
#: player's command. `SHAPE_PARAMS` in tools/generate_datatables.py allows
#: `FervourReserve` on these two and on nothing else.
RESERVING_SHAPES = ("Summon", "Deployable")

#: What a reservation is called in the free-text `ShapeParams` column.
RESERVE_KEY = "FervourReserve"

#: How many built skills carry a reservation, pinned so the number can only move
#: deliberately.
#:
#: FIVE ON 2026-09-09: Subjugate reserves 30 for each thrall, Summon Imp 10 for
#: each imp, and Bolt Turret, Ballista and Iron Fortress 5 for each gadget. Iron
#: Fortress places five deployables, so it reserves 25 between them.
#:
#: A NEW GADGET SKILL SHOULD RAISE THIS. If it falls, a skill stopped being a
#: summon or a deployable and somebody should say why.
RESERVING_SKILLS = 5


def skills() -> list[dict]:
    if not SKILLS_CSV.is_file():
        pytest.skip(f"{SKILLS_CSV.name} is not present. Run "
                    "python tools/generate_datatables.py")
    with SKILLS_CSV.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


@pytest.fixture(scope="module")
def rows() -> list[dict]:
    return skills()


def test_every_built_summon_or_deployable_reserves_fervour(rows):
    """The owner's rule, made checkable.

    A skill with one of the two shapes makes something that stays out under the
    player's command, and the design says the pool is what caps how many of
    those a character may have. A row with no reservation is one the cap does
    not reach.
    """
    free = [row["Name"] for row in rows
            if row["Shape"].strip() in RESERVING_SHAPES
            and RESERVE_KEY not in row["ShapeParams"]]

    assert not free, (
        f"These skills make a minion or a gadget and reserve no Fervour, so a "
        f"character may hold them for free: {free}. The project owner ruled on "
        f"2026-09-07 that every minion and gadget skill reserves, and "
        f"docs/DECISIONS.md records it. Add {RESERVE_KEY}=<amount> to the "
        f"skill's ShapeParams in the Weapon Skills sheet of "
        f"docs/All_Things_Cataclysm.xlsx and regenerate.")


def test_the_number_of_reserving_skills_is_what_was_measured(rows):
    """Pinned, so a gadget losing its reservation cannot pass as a rename.

    THE CHECK ABOVE ALONE WOULD NOT CATCH IT. A skill whose Shape changed from
    `Deployable` to blank leaves that check with nothing to complain about --
    the row is no longer a deployable, so it is no longer asked the question --
    and the gadget quietly stops costing anything.
    """
    reserving = [row["Name"] for row in rows
                 if RESERVE_KEY in row["ShapeParams"]]

    assert len(reserving) == RESERVING_SKILLS, (
        f"{len(reserving)} skills carry {RESERVE_KEY} and this test expects "
        f"{RESERVING_SKILLS}: {sorted(reserving)}. If a gadget was added or "
        f"removed, raise or lower RESERVING_SKILLS here and say which in the "
        f"pull request.")


def test_a_reservation_is_a_positive_number(rows):
    """A reservation of zero reads as a rule applied and is a rule skipped.

    It would pass both checks above and cost the player nothing, which is the
    failure this whole file exists to catch, wearing the costume of the fix.
    """
    wrong: list[str] = []
    for row in rows:
        for part in row["ShapeParams"].split(";"):
            key, _, value = part.partition("=")
            if key.strip() != RESERVE_KEY:
                continue
            try:
                amount = float(value.strip())
            except ValueError:
                wrong.append(f"{row['Name']} reserves {value.strip()!r}, "
                             "which is not a number")
                continue
            if amount <= 0:
                wrong.append(f"{row['Name']} reserves {amount:g}")

    assert not wrong, (
        "A reservation must be a positive amount of Fervour. These are not, so "
        "the skill reads as obeying the rule and costs nothing: "
        + "; ".join(wrong))
