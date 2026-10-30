"""Player decision policies.

These exist to answer one question: does it matter which dungeon you pick?

Every policy now chooses between three things, not two:
  * run a dungeon (defends a city, earns loot and materials, slow power gain)
  * go to the forge (fast power gain, defends nothing, burns materials)
  * do nothing

If a careless policy and a careful policy survive equally long, the empire
layer is not a game, it is a slideshow. The gap between the best and worst
policy is therefore the primary design signal this rig produces.

WHICH POLICIES SEE A SIEGE, AND WHY NOT ALL OF THEM. Issue #1340. A Siege's
damage happens every day it stands rather than when its timer fires, so it was
invisible to the only term any policy had for what a dungeon costs a city --
`d.defense_damage`, what ONE RESOLVE costs. The project owner ruled on
2026-09-06, verbatim: "Fix the policies soon, before more figures accumulate
(Recommended)".

`triage` and `lane_aware` are the two policies that actually weigh damage
prevented, and they are the two that were fixed. **The others are controls and
were deliberately left blind.** `random_pick` has to stay random, `greedy_loot`
has to go on ignoring the empire and `always_craft` has to go on letting cities
burn, or the gap this rig exists to measure closes because the floor came up
rather than because the ceiling did. `never_craft` and `nearest_deadline` sort
by a timer and compute no damage at all, so there is no calculation in them to
repair; teaching either one about a Siege would be giving it a new capability,
which is the thing the owner's ruling did not ask for.
"""

from __future__ import annotations

from .config import CityTier, DungeonType
from .engine import CRAFT

TIER_VALUE = {
    CityTier.OUTPOST: 1.0,
    CityTier.BULWARK: 3.0,
    CityTier.SANCTUARY: 8.0,
    CityTier.PILLAR: 100.0,
}


def _quest_timer_hits(sim, d) -> bool:
    """Whether this Quest dungeon's timer running out will hit its host.

    Since 2026-10-13 it does, through `Simulation._quest_hit`, unless
    `quest_timer_hits_city` is off or the host has fallen. Until 2026-10-16 no
    policy asked: `Dungeon.resolves` answers False for a Quest dungeon, so the
    timer was invisible to every list built from it.
    """
    if d.dtype is not DungeonType.QUEST or not sim.cfg.quest_timer_hits_city:
        return False
    return not sim.empire.cities[d.city_id].fallen


def _quest_hit_fells(sim, d, city) -> bool:
    """Whether the next hit of this Quest dungeon's timer would fell its host.

    The hit is the ordinary row of the host's tier, as `_quest_hit` takes it.
    NEITHER THE TREE'S REDUCTION NOR A DEPTH SCALE IS APPLIED, which is what
    the ordinary branch of each policy does with its own guess.
    """
    if not _quest_timer_hits(sim, d):
        return False
    # A HOLLOW'S HIT ADDS VOID STACKS, since 2026-10-18: it fells its city when
    # they reach the erase count.
    if sim._hollow_adds_stacks(d):
        return (city.void_stacks + sim.hollow_hit_stacks(d, city)
                >= sim.cfg.void_stacks_to_erase)
    return sim.cfg.spec(DungeonType.BASIC, city.tier).defense_damage >= city.defense


def quest_dungeons_to_finish(sim, d) -> int | None:
    """How many more quest dungeons the Cataclysm that sent `d` needs, or None
    when clearing `d` brings the Cataclysm dungeon no nearer. Issue #1388.

    THE RULE IT READS. The project owner ruled on 2026-09-06 that the boss
    opens when the quest objectives of HALF the active Cataclysms are met,
    rounded up (`TuningConfig.cataclysms_required`), each by its own count
    (`TuningConfig.quest_objectives_for`). Both policies used to read
    `quest_objectives_required - sim.objectives`: a flat 8, now only a fallback,
    minus every quest dungeon cleared from every Cataclysm. That said a player
    with eight objectives all from one of four Cataclysms was done, when they had
    finished one of the two the gate needs.

    NONE, so the policy adds no bonus, when `d`'s Cataclysm is already finished,
    or when enough are finished that the boss is open: in both cases the clear
    counts towards nothing the gate reads.

    COUNTED IN DUNGEONS AND NOT IN CATACLYSMS, which is a judgement rather than
    something the ruling settles. It keeps the `1 + 1 / remaining` bonus on the
    scale both policies were tuned at; a distance of one to four Cataclysms
    would have rescaled it, which is a tuning change and not this correction.

    A dungeon built by hand in a test names no Cataclysm and is measured against
    `quest_objectives_for("")`, the documented fallback. In a campaign every
    Quest dungeon names one: `trigger_surge` sets `source` on all it lands.
    """
    cfg = sim.cfg
    complete = sim.cataclysms_complete()
    if len(complete) >= cfg.cataclysms_required() or d.source in complete:
        return None
    done = sim.objectives_by_type.get(d.source, 0)
    return max(1, cfg.quest_objectives_for(d.source) - done)


def _resolve_fells(sim, d, city) -> bool:
    """Whether this ordinary dungeon's timer would fell its city.

    By the row's damage against the city's defence, as each policy always
    guessed; FOR A VOID DUNGEON, BY ITS STACKS, since 2026-10-18: its timer
    takes no defence, and erases the city when the stacks it adds
    (`Simulation._void_stacks_for`) bring the city to
    `TuningConfig.void_stacks_to_erase`. Part of the plan the coordinating
    session accepted on 2026-10-16; without it a policy ranked a Void dungeon
    by damage it does not do and never saw an erasure coming.
    """
    if sim._adds_void_stacks(d):
        return (city.void_stacks + sim._void_stacks_for(d)
                >= sim.cfg.void_stacks_to_erase)
    return d.defense_damage >= city.defense


def _safe(sim, dungeons):
    """Dungeons the player can attempt without an unacceptable death risk."""
    tol = sim.cfg.death_risk_tolerance
    return [d for d in dungeons if sim.death_chance(d) <= tol]


def _endgame(dungeons):
    """The Cataclysm dungeon ends the run. A real player takes that shot at any
    odds rather than sitting at the capital watching the empire burn, so it
    bypasses the risk filter entirely."""
    for d in dungeons:
        if d.dtype is DungeonType.CATACLYSM:
            return d
    return None


def _stuck(sim, dungeons) -> bool:
    """Nothing is safely attemptable -- power is the bottleneck, not time."""
    return bool(dungeons) and not _safe(sim, dungeons)


def siege_daily_damage(sim, d, city) -> float:
    """Points of defence this dungeon takes from its city EVERY DAY it stands.

    0.0 for anything that is not a Siege, which is what keeps this change out
    of every other dungeon's score.

    A MIRROR OF `Simulation._apply_siege_damage` AND NOT A SECOND OPINION. The
    flat share, the growth of 2.5 points a day, the day the growth counts from,
    the damage-reduction multiplier and the rule that a fallen city is not
    bitten again all have to match the engine, or a policy would be reasoning
    about damage the day loop does not actually deal.
    `tests/test_policies_see_sieges.py` drives the two against each other for
    that reason rather than restating the arithmetic. **The growth was ten
    points a day until the owner cut it on issue #1349 on 2026-09-06**, and this
    sentence still said ten afterwards, which is half of what issue #1364 is
    about. `tests/test_the_siege_prose_in_policies_is_true.py` now reads the
    figure out of this docstring and compares it against
    `TuningConfig.siege_damage_growth_per_day`.

    EVERY DUNGEON TYPE CAN CARRY THE SUB-TYPE, not only the ordinary ones:
    `_roll_subtype` runs for every `_make_dungeon` but a surge's Quest
    dungeon, and since 2026-10-16 that one takes its Cataclysm's fixed
    sub-type (`TuningConfig.QUEST_SUBTYPE_BY_CATACLYSM`), which for War is the
    Siege, at most two a wave. Measured on 2026-10-18 over 10,000 campaigns
    (seeds 0 to 9999, 756,907 dungeons, each recorded when it was made) at the
    settings `siege_urgency` names below, with an ordinary dungeon leaving
    after it hits its city (`dungeon_persists_after_resolve` False), the
    64,191 Sieges that reached the map were 70% Basic, 24% Quest, 5% Fallen
    City and 1% Cataclysm, about 6.42 a campaign. That is why the callers below
    apply this in all three scoring branches instead of only the ordinary one.
    UNTIL 2026-10-18 THIS SAID 47,102 Sieges at 95 / 0 / 4 / 2 and 4.71 a
    campaign, measured on 2026-10-16 while an interim rule barred a Quest
    dungeon from the Siege.
    UNTIL 2026-10-16 THIS SAID 55,361 Sieges at 83 / 11 / 4 / 1 and 5.54 a
    campaign, measured on 2026-10-12, when a Quest dungeon could roll a Siege.
    UNTIL 2026-10-12 IT SAID 65,636 Sieges at 79 / 11 / 10 / 1 and 6.56 a
    campaign, measured on 2026-09-16 with the dungeon staying.

    THE QUEST SHARE WENT TO NOUGHT ON 2026-10-16, AND THE OTHERS ROSE TO FILL
    IT. Over the same 10,000 seeds on commit 3a48236a: 721,405 dungeons made,
    81,589 of them Quest dungeons and 30,347 Fallen City; the four shares
    before rounding 94.73 / 0.00 / 3.63 / 1.63; the share of each kind that
    rolls a Siege, Basic 7.44%, Fallen City 5.64%, Cataclysm 7.79%. The model
    changed in other ways between the two measurements -- among them an
    opening wave of 12, the ordinary dungeon's damage at 0.7, and a Quest
    dungeon moving when its city falls -- so the fall in the rate is not the
    Quest bar's alone; what each change contributed was not measured.

    THE RATE FELL FROM 10.6 A CAMPAIGN AND THE FOUR SHARES DID NOT MOVE AT ALL.
    Issue #1357 changed when the Cataclysm dungeon opens, so campaigns end
    sooner and fewer dungeons of every kind reach the map. Which kinds carry a
    Siege is a property of `_roll_subtype` and not of how long a campaign runs,
    which is why one number moved and the other four did not -- and it is the
    shares rather than the rate that the sentence above exists to justify.
    THE RATE FELL AGAIN, FROM 8.97 TO 6.56, on issue #1432 (2026-09-16), when a
    city's fall stopped firing a surge inside `surge_interval_min`; the four
    shares moved by under a point (78 / 11 / 10 / 1 to 78.5 / 10.7 / 9.6 /
    1.1 before rounding), for the same reason.

    THE RATE FELL A THIRD TIME, FROM 6.56 TO 5.54, ON 2026-10-12, AND THIS TIME
    A SHARE MOVED WITH IT. An ordinary dungeon now leaves after it hits its
    city, so fewer cities fall and fewer Fallen City dungeons are made. Over
    the same 10,000 seeds, with the dungeon staying and with it leaving:
    891,123 dungeons against 754,158; Fallen City dungeons made, 95,660 against
    37,679; Sieges, 65,637 against 55,361; the four shares before rounding,
    78.02 / 10.85 / 9.97 / 1.16 against 83.45 / 11.46 / 3.63 / 1.46. The share of
    each KIND that rolls a Siege moved little for three of them (Basic 7.41%
    to 7.42%, Quest 7.51% to 7.52%, Cataclysm 8.11% to 8.25%) and fell for the
    Fallen City (6.84% to 5.33%); why that one fell was not measured. So the
    sentence two paragraphs up, that the shares are a property of
    `_roll_subtype` and not of the campaign, holds for the roll and not for
    the mix: the mix follows how many dungeons of each kind are made. The run
    with the dungeon staying is the control for the method; it gives 65,637
    Sieges where the 2026-09-16 record has 65,636, on code that has changed
    since.

    THE COUNTS THAT USED TO BE HERE -- 114 Basic, 28 Fallen City, 11 Quest and 3
    Cataclysm over twelve campaigns -- were taken before two changes of the same
    day. The owner halved the Siege spawn weight on issue #1349, and a surge
    began rolling Quest dungeons on issue #1324. Twelve campaigns could not have
    separated Quest from Fallen City in any case, and the order was wrong.
    """
    if d.subtype != "Siege" or city is None or city.fallen:
        return 0.0
    cfg = sim.cfg
    # AS `Simulation._apply_siege_damage` BITES, since 2026-10-17: the growth
    # counts the days on this city, and a Quest Siege bites a quarter as hard.
    days_stood = d.days_on_its_city(sim.day)
    grown = cfg.siege_damage_growth_per_day * days_stood
    return ((city.max_defense * cfg.siege_defence_bite_per_day + grown)
            * cfg.tree.damage_taken(cfg.active_cataclysm_count())
            * d.siege_bite_share(cfg.quest_siege_bite_scale))


def siege_damage_during_the_walk(sim, d, city) -> float:
    """Defence the Siege takes from its city between now and the player
    finishing this dungeon. 0.0 for anything that is not a Siege.

    ENTERING A SIEGE DOES NOT PAUSE IT. `Simulation.step` applies the daily
    bite before any timer and wherever the player is, so a Siege goes on eating
    the city for every day of the walk and the walk is part of the cost of
    answering it. That is the opposite of every other dungeon, whose timer
    stops while the player is inside it.

    The growth is included, because it does not stop either: over `k` days the
    bite rises by `siege_damage_growth_per_day` each day, which is the closed
    form below rather than `k` times today's bite.
    """
    per_day = siege_daily_damage(sim, d, city)
    if per_day <= 0.0:
        return 0.0
    cfg = sim.cfg
    k = max(1, d.run_days)
    growth = (cfg.siege_damage_growth_per_day
              * cfg.tree.damage_taken(cfg.active_cataclysm_count())
              * d.siege_bite_share(cfg.quest_siege_bite_scale))
    return k * per_day + growth * k * (k - 1) / 2.0


def siege_urgency(sim, d, city, fatal_mult: float) -> float:
    """How much more this dungeon is worth clearing because a Siege stands on
    it. Exactly 1.0 -- no change at all -- for everything that is not one.

    AN UNATTENDED SIEGE EMPTIES ITS CITY IF IT STANDS LONG ENOUGH: 25 days for
    an Outpost, 39 for a Bulwark, 55 for a Sanctuary, 70 for the Pillar. This
    function treats "will this city die?" as settled before the policy is
    asked, so the only question it asks is **whether the player can still get
    there in time**. This returns the policy's own `fatal` multiplier when they
    can and 1.0 when they cannot.

    THIS PARAGRAPH SAID "AN UNATTENDED SIEGE ALWAYS KILLS ITS CITY" UNTIL
    2026-10-12, AND FOR A SIEGE ON AN ORDINARY DUNGEON THAT IS NO LONGER TRUE.
    Since that day `TuningConfig.dungeon_persists_after_resolve` is False: an
    ordinary dungeon hits its city once when its timer runs out and leaves the
    map, and the Siege it carries leaves with it. Its timer is rolled from its
    depth and can be shorter than the days above. Measured on 2026-10-12: one
    ordinary dungeon carrying a Siege on a city at full defence, nothing else
    on the city, nobody entering, no tree, 10,000 dungeons for each city size
    (seeds 0 to 9999), each day driven through `_apply_siege_damage` and then
    `_resolve` in the order `Simulation.step` uses.

      * At the shipped defaults the city fell in 86.1% of cases on an
        Outpost, 79.0% on a Bulwark and 86.7% on a Sanctuary: to the daily
        damage in 81.9% / 70.5% / 78.7%, and to the one hit at the timer's
        end in 4.2% / 8.6% / 8.0%. In the other 13.9% / 20.9% / 13.3%
        the dungeon left first and the city stood, on a median of 12.4% /
        8.7% / 7.8% of its defence. The timers rolled were 19 to 39, 28 to 57
        and 42 to 85 days.
      * At the report settings named below, where the timer is longer
        (`resolve_floor_ratio` 2.0 against 1.6; timers of 22 to 45, 34 to 68
        and 51 to 103 days), the city fell in 97.0% / 96.8% / 99.8% of cases.
      * The control: at the shipped defaults with the dungeon staying, the
        city fell in all 10,000 cases at each size.

    A city already damaged falls sooner, and that was not measured. A Siege on
    a Quest dungeon, a Fallen City dungeon or the Cataclysm is not removed by
    that rule. THE CODE BELOW DOES NOT KNOW ANY OF THIS: it still weighs a
    Siege the player can reach as certain to kill its city, whatever kind of
    dungeon carries it. On 2026-10-12 this text changed and the code did not.

    THOSE FOUR WERE 14 / 23 / 34 / 47 until the owner cut the growth from ten
    points a day to 2.5 on issue #1349 on 2026-09-06. They are not targets: the
    constant moved and they followed it. The same day loop still returns the old
    four when the growth is put back to ten, which is how the derivation is
    checked -- `tests/test_the_siege_prose_in_policies_is_true.py` drives both.

    WHY IT IS THAT SHAPE AND NOT A SLIDING SCALE. The first version of this
    scaled with the share of the city the Siege would eat during the walk, and
    it was backwards: it gave FULL weight to Sieges that were already
    unwinnable, because they eat all of it, and the LEAST weight to a Siege
    that had just arrived, which is the only moment it can still be answered.
    Measured over 600 campaigns it moved the acceptance ratio from 1.01 to
    1.03, which is to say barely at all.

    WHY THE DISTINCTION STILL MATTERS NOW THAT THE MARGIN IS NOT TIGHT. At
    difficulty tier 1 -- one active Cataclysm -- with no empire tree, `triage`,
    and static surges of five dungeons every 120 days, the median walk is 14
    days to an Outpost, 23 to a Bulwark, 35 to a Sanctuary and 123 to the
    Pillar. Against the 25 / 39 / 55 days a fresh Siege leaves those three
    sizes, the slack is 11 / 16 / 20 days. Measured on 2026-10-18 over 10,000
    campaigns (seeds 0 to 9999) and 756,907 dungeons, each recorded at the
    moment it was made, with an ordinary dungeon leaving after it hits its city
    (`dungeon_persists_after_resolve` False, the default since 2026-10-12),
    each Cataclysm's Quest dungeons one fixed sub-type and War's at most two a
    wave. UNTIL 2026-10-18 THIS SAID 14 / 22 / 34 / 123 AND A SLACK OF
    11 / 17 / 21, measured on 2026-10-12 over 754,158 dungeons. UNTIL 2026-10-12
    IT SAID 14 / 24 / 37 / 123 AND A SLACK OF 11 / 15 / 18,
    measured on 2026-09-16 over 890,790 dungeons with the dungeon staying;
    issues #1364 and #1432.

    QUOTE THOSE TO THE DAY AND NO FINER, AND RE-MEASURE RATHER THAN CARRYING
    THEM FORWARD. The walk lengths are nearly uniform where the median sits --
    48.17% of Outpost dungeons walk in 13 days or fewer and 56.35% in 14 or
    fewer, in the 2026-10-12 measurement above (47.3% and 54.9% on 2026-09-16)
    -- so the median is a coin flip between two adjacent days, and a block of
    320 campaigns lands on either side of it at random. That is how this file
    came to state 12 / 20 / 33 while issue #1364 measured 14 / 22 / 33 on the
    same code: neither was reproducible, and neither was wrong by much.
    `tests/test_the_siege_prose_in_policies_is_true.py` re-measures all four and
    allows a day either way.

    IT MOVED WHILE THIS PARAGRAPH WAS BEING WRITTEN, WHICH IS THE ARGUMENT FOR
    THE GUARD. Issue #1369 put the Cow Level spawn weight back to 7 from the 7.6
    the Siege rescale of issue #1349 had lifted it to, and a Cow Level walks in
    twice the days. The Sanctuary median fell from 35 to 34 between the first
    draft of this paragraph and the second, and the guard failed in continuous
    integration rather than a reader finding it a month later. Re-measured on
    `e8b33c2`.

    AND IT MOVED AGAIN, FOR A REASON THAT IS NOT ABOUT WALKS AT ALL. Issue #1357
    changed WHEN A CAMPAIGN ENDS: the Cataclysm dungeon used to open at a flat
    total of 8 quest objectives and now opens when half the active Cataclysms,
    rounded up, have each met their own count -- at tier 1 that is the one
    active Cataclysm's own number, which is 5, 8 or 10 depending on which one
    the character drew. Campaigns therefore run to different lengths, the mix of
    city sizes a surge has left to hit changes with them, and every figure above
    is an average over that mix. The three medians rose by 1, 1 and 3 days and
    the sample fell from 1,488,436 dungeons to 1,250,908. **Nothing about a walk
    changed.** Re-measured over 10,000 campaigns on 2026-09-06; the guard failed
    first, which is what it is for.

    THE OUTPOST MEDIAN SAT HIGHER INSIDE ITS OWN BIN AFTER #1357 THAN BEFORE,
    and that was worth knowing before the next change moved it again. On
    2026-09-06, 56.2% of Outpost
    dungeons walk in the stated 14 days or fewer, where the figure was 50.5% at
    the stated 13. The guard asks for that share to be between 44% and 58%, so
    the headroom above is now under two points rather than seven and a half. It
    passed; the next thing that lengthens a campaign was likely to trip it, and
    the answer then is to re-measure rather than to widen the range. Issue #1389
    carries what to look at when it does.

    AND IT MOVED A THIRD TIME, ON ISSUE #1432, FOR A REASON THAT IS ABOUT HOW
    MANY SURGES LAND. A city's fall used to fire a surge whatever the gap since
    the last one; from 2026-09-16 it respects `surge_interval_min` like the
    other two triggers. Fewer surges land, fewer cities fall, campaigns run
    longer, and the mix of city sizes a surge has left to hit changes again.
    Re-measured over 10,000 campaigns on 2026-09-16: the Bulwark median rose
    from 23 to 24 and the other three held; the sample fell from 1,250,908
    dungeons to 890,790; the Outpost share at 14 days or fewer fell from 56.2%
    to 54.9%, so the headroom under the guard's 58% is three points. The guard
    failed first, again, which is what it is for; the day loop's before-and-
    after figures are in the decisions log entry for #1432.

    AND IT MOVED A FOURTH TIME, ON 2026-10-12, WHEN AN ORDINARY DUNGEON BEGAN
    TO LEAVE AFTER IT HITS ITS CITY. Fewer cities fall. Re-measured that day
    over 10,000 campaigns (seeds 0 to 9999) under each rule: the Bulwark
    median fell from 24 to 22 and the Sanctuary median from 37 to 34; the
    Outpost and the Pillar held at 14 and 123. The sample fell from 891,123
    dungeons to 754,158: 685,247 to 687,981 on an Outpost, 155,635 to 47,007
    on a Bulwark, 40,118 to 8,572 on a Sanctuary and 10,123 to 10,598 on the
    Pillar. Which dungeons stopped being made on the two larger sizes, by
    kind, was not measured. The Outpost share at 14 days or fewer rose from
    54.67% to 56.35%, so the headroom under the guard's 58% is 1.65 points,
    and over ten disjoint blocks of 320 campaigns one block (seeds 2240 to
    2559) gave 58.16%. The guard reads seeds 0 to 319, which give 56.49%. THE
    GUARD DID NOT FAIL FIRST THIS TIME: the change that moved the rule pinned
    the guard to the old rule, and the prose was corrected afterwards in a
    second change. The guard now runs 320 campaigns, because 250 made 18,654
    dungeons at the new rule and its fixture asks for more than 20,000.

    THE SETTINGS ARE PART OF THE FIGURE. Five dungeons a surge is what the
    balance report uses and NOT `TuningConfig.surge_dungeon_count`, which was 4
    when this was measured. Since 2026-10-13 that default is 5 and the
    default interval is 90 days; the figures here are at 5 every 120.
    The two are different worlds. THE FOUR MEDIANS WERE THE SAME IN BOTH ON
    2026-10-12 (not re-measured at four since), and the samples were not:
    measured that day over 10,000
    campaigns each (seeds 0 to 9999) with the dungeon leaving, four a surge
    gives 14 / 22 / 34 / 123 over 720,044 dungeons and five gives 14 / 22 / 34
    / 123 over 754,158. With the dungeon staying the same seeds give 14 / 24 /
    38 / 124 at four and 14 / 24 / 37 / 123 at five. This paragraph said until
    then that at the larger city sizes the medians differ by more than the day
    of wobble above; neither pair measured on 2026-10-12 does. Issue #1286
    names the block.

    THAT MAKES THE ANSWER REACHABLE AND NOT AUTOMATIC. A Siege that has already
    stood for a while is still hopeless, and this returns 1.0 for it. So is
    every Siege on the Pillar, and that one is exact rather than typical. A
    surge cannot target the Pillar, so the only dungeons that spawn there are
    the Cataclysm at 100 to 150 floors and, once the Pillar falls, its own
    Fallen City at 80 to 120 -- and both openers only ever ADD floors to a
    Cataclysm. The shortest walk to any of them is therefore 80 days against the
    70 a fresh Siege leaves, so a Pillar Siege can never be answered: not at the
    median, and not at the best roll the model can produce. What changed is that
    the reachable case is now common instead of vanishing, which is why the
    policy has to be able to tell the two apart at all.

    WHY IT IS ANCHORED TO THE POLICY'S OWN `fatal` MULTIPLIER RATHER THAN TO A
    NEW CONSTANT. Each policy already has a number for "the damage about to
    land kills this city": triage multiplies by 5, lane_aware by 4. A Siege the
    player can still reach is that same statement about a different clock, so
    it earns the same weight and no more. Inventing a separate weight would
    have been a third balance number to tune with nothing anchoring it.
    """
    during = siege_damage_during_the_walk(sim, d, city)
    if during <= 0.0:
        return 1.0
    if during >= city.defense:
        # The city falls before the player could arrive, so there is nothing
        # left to save by going. A policy that chased it anyway would spend the
        # walk losing the city it went to defend AND whatever it walked past.
        return 1.0
    return fatal_mult


# ---------------------------------------------------------------------------

def random_pick(sim, dungeons):
    """Baseline incompetence. Picks a safe dungeon at random; crafts only when
    nothing safe is standing and it has the materials."""
    safe = _safe(sim, dungeons)
    if not safe:
        return CRAFT if sim.can_craft() else None
    return sim.rng.choice(safe)


def never_craft(sim, dungeons):
    """Pure defence. Refuses the forge entirely -- the control group for
    whether crafting time is worth its cost."""
    safe = _safe(sim, dungeons)
    if not safe:
        return None
    live = [d for d in safe if d.resolves]
    return min(live, key=lambda d: d.resolve_in) if live else max(
        safe, key=lambda d: d.floors)


def always_craft(sim, dungeons):
    """The other extreme: forge whenever materials allow, let cities burn."""
    if sim.can_craft():
        return CRAFT
    safe = _safe(sim, dungeons)
    if not safe:
        return None
    return max(safe, key=lambda d: d.floors)


def nearest_deadline(sim, dungeons):
    """Always answer the fire that is about to go off. Crafts only when
    genuinely blocked."""
    end = _endgame(dungeons)
    if end is not None:
        return end
    safe = _safe(sim, dungeons)
    if not safe:
        return CRAFT if sim.can_craft() else None
    live = [d for d in safe if d.resolves]
    if not live:
        return max(safe, key=lambda d: d.floors)
    return min(live, key=lambda d: d.resolve_in)


def greedy_loot(sim, dungeons):
    """Ignore the empire, farm the deepest thing you can survive."""
    safe = _safe(sim, dungeons)
    if not safe:
        return CRAFT if sim.can_craft() else None
    return max(safe, key=lambda d: d.floors)


def triage(sim, dungeons):
    """The policy a good player would converge on.

    Weighs three things against each other: damage prevented per day, progress
    toward the win, and the fact that power is itself a prerequisite for both.
    Crafts when the empire is stable enough to afford the detour, or when it
    is underpowered and slogging on would be worse.
    """
    cfg = sim.cfg
    end = _endgame(dungeons)
    if end is not None:
        return end

    safe = _safe(sim, dungeons)
    danger = sim.empire.breach_depth() / 3.0

    # Power bottleneck: nothing safely runnable, or the Cataclysm dungeon is
    # sitting there out of reach. Forge time is the only way out.
    if sim.can_craft():
        if _stuck(sim, dungeons):
            return CRAFT
        # Comfortable empire and materials to burn -> bank power now, because
        # the endgame dungeon needs far more than looting alone will supply.
        if danger == 0.0 and sim.materials >= cfg.craft_material_cost * 1.5:
            hot = [d for d in safe
                   if (d.resolves or _quest_timer_hits(sim, d)) and d.resolve_in <= cfg.craft_days]
            if not hot:
                return CRAFT

    if not safe:
        return None

    best, best_score = None, float("-inf")
    for d in safe:
        city = sim.empire.cities[d.city_id]
        # A Siege bites daily whatever KIND of dungeon carries it, so this is
        # worked out once and applied in all three branches below. Issue #1340.
        siege = siege_urgency(sim, d, city, 5.0)

        if d.dtype is DungeonType.QUEST:
            # HOW FAR THIS DUNGEON'S OWN CATACLYSM IS FROM FINISHED, since
            # issue #1388, and no bonus once clearing it brings the boss no
            # nearer. See `quest_dungeons_to_finish`.
            remaining = quest_dungeons_to_finish(sim, d)
            value = 14.0 * (1.0 - 0.6 * danger) * (1.0 + (1.0 / remaining if remaining else 0.0))
            # ITS TIMER HITS ITS HOST, since 2026-10-13, and this policy saw
            # it from 2026-10-16: a hit that would fell the host weighs what a
            # fatal ordinary hit weighs here.
            if _quest_hit_fells(sim, d, city):
                value *= 5.0
            score = value * siege / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if d.dtype is DungeonType.FALLEN_CITY:
            value = TIER_VALUE[d.city_tier] * 4.0
            score = value * siege / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if not d.resolves:
            continue

        # NEITHER THE DEPTH SCALE NOR THE TREE IS APPLIED, which was true of
        # the fraction this replaced as well. A policy guesses; issue #1327
        # changed the shape of the number and deliberately not the guess.
        fatal = _resolve_fells(sim, d, city)
        value = TIER_VALUE[d.city_tier] * (1.0 + (1.0 - city.defense_frac) * 3.0)
        if fatal:
            value *= 5.0

        # Entering pauses this dungeon's timer, so the only question is whether
        # it survives long enough for the player to arrive at all.
        #
        # A SIEGE STILL WORTH ANSWERING IS EXEMPT FROM THAT DISCOUNT, and it is
        # the same defect as the one above rather than a second change. The
        # discount says a dungeon whose timer has already fired has already
        # spent its damage; a Siege has spent none of it, because its damage is
        # not at resolve time. Discounting it would rank a Siege LOWEST exactly
        # while it is doing the most harm. Issue #1340.
        if d.resolve_in <= 0 and siege <= 1.0:
            value *= 0.2

        score = value * siege / max(1, d.run_days)
        if score > best_score:
            best, best_score = d, score

    if best is None:
        return max(safe, key=lambda d: d.floors) if safe else None
    return best


def lane_aware(sim, dungeons):
    """Triage, but scoring cities by what losing them does to the MAP rather
    than by their tier.

    The run does not end when you have lost a lot of cities; it ends when three
    fall in a line. So a city is worth defending in proportion to how much its
    loss would shorten the Cataclysm's path to the Pillar -- and cities off
    every shortest lane are, structurally, free to give away.
    """
    cfg = sim.cfg
    end = _endgame(dungeons)
    if end is not None:
        return end

    safe = _safe(sim, dungeons)
    crit = sim.empire.lane_criticality()
    d2d = sim.empire.distance_to_defeat()
    # 3 = untouched, 0 = the Last Stand is firing.
    peril = (3 - d2d) / 3.0

    if sim.can_craft():
        if _stuck(sim, dungeons):
            return CRAFT
        # Forge only while no lane is genuinely threatened.
        if d2d >= 3 and sim.materials >= cfg.craft_material_cost * 1.5:
            hot = [d for d in safe
                   if (d.resolves or _quest_timer_hits(sim, d)) and d.resolve_in <= cfg.craft_days]
            if not hot:
                return CRAFT

    if not safe:
        return None

    best, best_score = None, float("-inf")
    for d in safe:
        city = sim.empire.cities[d.city_id]
        # As in `triage`, and for the same reason: the sub-type rides on every
        # kind of dungeon, so this cannot live in the ordinary branch alone.
        # The weight is this policy's own `fatal` multiplier. Issue #1340.
        siege = siege_urgency(sim, d, city, 4.0)

        if d.dtype is DungeonType.QUEST:
            # HOW FAR THIS DUNGEON'S OWN CATACLYSM IS FROM FINISHED, since
            # issue #1388, and no bonus once clearing it brings the boss no
            # nearer. See `quest_dungeons_to_finish`.
            remaining = quest_dungeons_to_finish(sim, d)
            value = 14.0 * (1.0 - 0.7 * peril) * (1.0 + (1.0 / remaining if remaining else 0.0))
            # ITS TIMER HITS ITS HOST, since 2026-10-13, and this policy saw
            # it from 2026-10-16: a hit that would fell the host weighs what a
            # fatal ordinary hit weighs here.
            if _quest_hit_fells(sim, d, city):
                value *= 4.0
            score = value * siege / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if d.dtype is DungeonType.FALLEN_CITY:
            # Retaking re-seals a lane. Worth most when it is THE lane.
            reseal = 1 + 8 * crit.get(city.cid, 0)
            value = 6.0 * reseal * (1.0 + 2.0 * peril)
            score = value * siege / max(1, d.run_days)
            if score > best_score:
                best, best_score = d, score
            continue

        if not d.resolves:
            continue

        # A city off every shortest lane is worth little no matter its tier.
        lane = 1.0 + 9.0 * crit.get(city.cid, 0)
        fatal = _resolve_fells(sim, d, city)
        value = lane * (1.0 + (1.0 - city.defense_frac) * 2.0) * (1.0 + 2.0 * peril)
        if fatal:
            value *= 4.0
        # A Siege still worth answering is exempt from the fired-timer discount
        # -- it has not spent its damage, it is still spending it. Issue #1340.
        if d.resolve_in <= 0 and siege <= 1.0:
            value *= 0.2

        score = value * siege / max(1, d.run_days)
        if score > best_score:
            best, best_score = d, score

    if best is None:
        return max(safe, key=lambda d: d.floors) if safe else None
    return best


#: How many more cities must still stand between the Cataclysm and the Pillar
#: after a city falls for `sacrifice_to_craft` to let that city go. Two means a
#: sacrifice never leaves the run one fall from the Last Stand.
SACRIFICE_KEEPS_DISTANCE = 2


def sacrifice_to_craft(sim, dungeons):
    """Triage, but willing to let a city fall to keep its time at the forge.

    THE RULE, IN ONE SENTENCE. Do what `triage` does, and also go to the forge
    when `triage` would not, if the player has the materials for a craft and
    every dungeon due to hit during the craft stands on a city that, if it
    fell, would still leave at least two more cities to fall before the
    Cataclysm reaches the Pillar.

    "Due to hit during the craft" is a dungeon whose timer runs out within
    `craft_days` and whose timer costs its city something: an ordinary dungeon
    that resolves, or a Quest dungeon whose timer hits its host. "Two more
    cities" is `Empire.distance_to_defeat` after that city's fall, read through
    `lane_criticality` (the drop its fall would cause); see
    `SACRIFICE_KEEPS_DISTANCE`. At the start of a campaign that lets any
    Outpost go and defends every Bulwark and Sanctuary; as cities fall, it
    defends more of what is left. When nothing is due at all, the condition
    holds and it crafts. Each due dungeon is judged on its own, not the set of
    them falling together.

    WHY IT EXISTS. Issue #1454. The project owner, 2026-09-07: a player "will
    have to decide when to sacrifice a city to craft and when not to". No
    other policy here makes that trade: `triage` and `lane_aware` stop crafting
    whenever any dungeon is due within a craft, and `always_craft` crafts
    whatever is due.
    """
    cfg = sim.cfg
    if _endgame(dungeons) is None and sim.can_craft():
        crit = sim.empire.lane_criticality()
        distance = sim.empire.distance_to_defeat()
        due = [d for d in dungeons
               if (d.resolves or _quest_timer_hits(sim, d))
               and d.resolve_in <= cfg.craft_days]
        if all(distance - crit.get(d.city_id, 0) >= SACRIFICE_KEEPS_DISTANCE
               for d in due):
            return CRAFT
    return triage(sim, dungeons)


def chase_quests(sim, dungeons):
    """Triage, but going after the quest dungeons that open the boss first.

    THE RULE. Take the Cataclysm dungeon whenever one is open, as every policy
    here does. Otherwise, of the dungeons safe to enter, take a Quest dungeon
    sent by one of the active Cataclysms still needed to open the boss -- the
    ones nearest their own quest count, as many as are still missing from half
    the active Cataclysms, rounded up -- with the shortest walk first. When none
    of those is safe to enter, do what `triage` does.

    WHY IT EXISTS. At difficulty tiers 4 to 8 a run is lost to the Last Stand
    before the boss can open: on the day it opened, the Cataclysms nearest
    finished had about half their quest dungeons cleared at tiers 5 and 6,
    because `triage` spends its days defending cities. This is the play style the
    coordinating session adopted on 2026-10-30 with the project owner's "Falling
    wins". `triage` is left as it was, because every recorded design figure and
    many tests name it; `docs/DECISIONS.md`, 2026-10-30, says which figures use
    which.
    """
    end = _endgame(dungeons)
    if end is not None:
        return end
    cfg = sim.cfg
    need = {t: cfg.quest_objectives_for(t) for t in sim.active_types}
    unfinished = [t for t in sim.active_types
                  if sim.objectives_by_type.get(t, 0) < need[t]]
    missing = cfg.cataclysms_required() - (len(sim.active_types) - len(unfinished))
    nearest = set(sorted(unfinished,
                         key=lambda t: need[t] - sim.objectives_by_type.get(t, 0))
                  [:max(0, missing)])
    wanted = [d for d in _safe(sim, dungeons)
              if d.dtype is DungeonType.QUEST and d.source in nearest]
    if wanted:
        return min(wanted, key=lambda d: d.run_days)
    return triage(sim, dungeons)


ALL = {
    "random": random_pick,
    "greedy_loot": greedy_loot,
    "never_craft": never_craft,
    "always_craft": always_craft,
    "nearest_deadline": nearest_deadline,
    "triage": triage,
    "lane_aware": lane_aware,
    "sacrifice_to_craft": sacrifice_to_craft,
    "chase_quests": chase_quests,
}
