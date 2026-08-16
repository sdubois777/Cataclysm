"""How one incoming hit becomes lost health.

WHAT THIS IS FOR. `docs/Cataclysm_GDD_v2.md` names every defensive stat and never
says how any of them combine. The consequence is concrete: the Ritualist's 832
energy shield and the Ravager's 371 armor are both declared, replicated and
completely inert, because nothing reads them. Issue #93.

This module is a proposal for the order and the formulas, written so the numbers
can be looked at rather than argued about.

WHAT IS ALREADY DECIDED AND IS NOT UP FOR DEBATE HERE:

    Evasion avoids an attack completely, but only a direct attack. Area damage
    lands regardless. Soft cap 60%, exceedable.

    Block is a chance, and a block removes 50% of the hit rather than preventing
    it. It applies to area damage as well as direct attacks. No cap.

    Resistances cap at 70% and may be over-capped by affixes.

Everything else below is proposed.

WHAT THIS DELIBERATELY DOES NOT DO. It takes the incoming damage as a parameter.
There are still no enemy damage numbers anywhere in the project, so this answers
"of a hit of X, how much reaches health" and not "how big is a hit". Those are
separate problems and the second one is not blocked by this.
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field

# --------------------------------------------------------------------------
# Armor
# --------------------------------------------------------------------------

#: Armor is converted to a percentage by `armor / (armor + K)`, where K rises
#: with the difficulty tier.
#:
#: Why this shape rather than subtracting armor from damage: it never reaches
#: 100%, so no amount of armor is immunity, and it has natural diminishing
#: returns, so the first points of armor matter most.
#:
#: Why K rises with tier: at a fixed K, armor earned early would keep its value
#: forever and gear would stop mattering. At 800 per tier, the Ravager's 371 base
#: armor is worth 32% at tier 1 and 5% at tier 8, so the class identity holds
#: early and gear has to carry it later.
ARMOR_CONSTANT_PER_TIER = 800.0

#: Even with the asymptote, armor alone should not approach immunity.
ARMOR_REDUCTION_CAP = 75.0

# --------------------------------------------------------------------------
# Resistance
# --------------------------------------------------------------------------

RESISTANCE_CAP = 70.0

#: How high the cap itself can be raised, and the reason there is a limit.
#:
#: The 70 above is where a resistance caps for a character who has done nothing
#: about it. It is a SOFT cap in the sense that resistance above it is worth
#: having, because penetration is subtracted before the cap is applied. It is
#: not soft in the sense of being exceeded: 70 is still all the mitigation the
#: character gets.
#:
#: MAXIMUM RESISTANCE RAISES THE CAP. One enchantment does it -- "You have +10
#: maximum resists" in `game/Data/EnchantmentsPositive.csv`, at weight 1, the
#: rarest and most powerful tier -- and three negative enchantments lower it.
#: Nothing else does, and no affix may; see the Maximum Resistance subsection of
#: `Cataclysm_GDD_v2.md`.
#:
#: WITHOUT A CEILING, STACKING REACHES IMMUNITY. Damage taken is proportional to
#: (100 - resistance), so the last few points are worth far more than the first
#: few: 70 to 80 removes a third of what still gets through, 80 to 90 removes
#: half of that again, and 100 removes all of it. A modifier whose value rises
#: as you take more of it needs a hard stop rather than a tuning pass.
#:
#: 90 IS PATH OF EXILE'S NUMBER, taken because it is the only figure available
#: from a shipped game and it exists for this exact reason. Its resistances cap
#: at 75 and its maximum resistance is hard capped at 90, reached in 1% steps
#: from rare modifiers. Here the base is 70 and one enchantment gives 10, so two
#: of them reach the ceiling and a third is wasted. That is the property the
#: ceiling is for: no amount of stacking reaches immunity.
#:
#: Issue #215.
MAX_RESISTANCE_CEILING = 90.0

#: Negative resistance means taking extra damage, which several enchantments
#: inflict deliberately. This bounds how bad it can get.
RESISTANCE_FLOOR = -100.0

# --------------------------------------------------------------------------
# Block
# --------------------------------------------------------------------------

BLOCK_DAMAGE_REDUCTION = 50.0

# --------------------------------------------------------------------------
# Weapon sub-types
# --------------------------------------------------------------------------

#: From the Weapon Sub-Types table: piercing ignores 20% of enemy armor,
#: slashing does 10% more damage versus HP, and magic 10% more versus shields.
PIERCING_ARMOR_IGNORED = 20.0
SUBTYPE_BONUS = 10.0

# --------------------------------------------------------------------------
# Blunt
# --------------------------------------------------------------------------

#: Blunt no longer does "10% more damage versus armor". That reading put it in
#: direct competition with piercing, which already beats armor and has a whole
#: family of affixes scaling it -- ignoring armor appears at least six times
#: across the enchantment tables, while nothing anywhere scales damage against
#: armored targets. Blunt was a flat 10% with nowhere to go.
#:
#: Instead it stuns. Stun is already a designed mechanic rather than a new one:
#: `Keyword.CC` is a generated gameplay tag, several War skills stun for between
#: 0.75 and 3 seconds, one ultimate grants immunity to it, and the character
#: sheet already carries crowd control resistance to defend against it.
BLUNT_STUN_CHANCE = 10.0

#: How long a stun lasts when something gave an ordinary hit a chance to apply
#: one, before the overflow rule below lengthens it.
#:
#: Deliberately the shortest duration any designed skill uses, matching the
#: Lunge skill's 0.75 seconds. Something that can stun on every hit must not
#: outclass the skills whose entire purpose is stunning, which run to 3 seconds.
#:
#: IT WAS CALLED `BLUNT_STUN_SECONDS` UNTIL 2026-08-16, when the project owner
#: settled that an affix grants a chance to stun and that blunt's 10% is part of
#: the same pool. A stun rolled from that pool lasts this long whatever weapon is
#: held, so a name mentioning only blunt had stopped being true.
INCIDENTAL_STUN_SECONDS = 0.75


# --------------------------------------------------------------------------
# The anti-stun-lock rule
# --------------------------------------------------------------------------
#
# THE REQUIREMENT, from the project owner: crowd control must not become tedious
# the way it is in many games in the genre, where the smallest hit can stun and a
# player can be stun-locked until they die. Issue #216.
#
# ANSWERED 2026-08-05, and it is a combination of two mechanisms rather than one:
#
#   1. A target with a lot of health is not stunned by small hits AT ALL.
#   2. A target that IS stunned cannot be stunned again for at least 5 seconds.
#   3. Bosses are immune to stun outright.
#
# BOTH OF THE FIRST TWO ARE NEEDED. A damage threshold alone still allows chain
# stunning by large hits. An immunity window alone still allows constant
# interruption by small ones.
#
# ONLY THE FIRST AND THIRD LIVE HERE. The immunity window is a rule about time
# and this module resolves one hit with no clock, so the game enforces it. What
# this module can do, and does, is refuse a stun the threshold forbids and refuse
# one against a boss.

#: How much of the target's MAXIMUM health a hit must deal before it can stun at
#: all, as a percentage. Below this the stun chance is zero however high the
#: attacker's chance to stun is.
#:
#: 10% IS THE MIDDLE OF WHAT THE GENRE SHIPS, and the three surveyed games do not
#: agree:
#:
#:     Last Epoch     more than 5% of maximum health
#:     Path of Exile  more than about 10% of effective maximum life, because a
#:                    computed stun chance at or below 20% is discarded
#:     Path of Exile 2  15%, below which the chance is zero
#:
#: Taking the middle rather than the strictest is deliberate: this design also
#: has a 5 second immunity window, which is longer than Last Epoch's 1 second and
#: longer than the 4 seconds Path of Exile gives its unique bosses. The window is
#: doing most of the anti-lock work, so the threshold does not also need to be
#: the harshest of the three.
STUN_DAMAGE_THRESHOLD = 10.0

#: How long a target cannot be stunned again after being stunned, in seconds.
#: Stated by the project owner as "at least 5 seconds". Enforced by the game
#: rather than here, and recorded here so both copies of the rule sit together.
STUN_IMMUNITY_SECONDS = 5.0

#: The longest a stun may last however much chance to stun is stacked, in
#: seconds.
#:
#: DERIVED, NOT PICKED. It is the longest stun anywhere in the design: the
#: Brute's Heart ten-piece set bonus in `game/Data/EnchantmentsPositive.csv`,
#: which stuns for 3 seconds and is the most expensive thing in the game to
#: assemble. Player skills run 0.75 to 1.5. So a blunt weapon with heavy
#: investment reaching the same hold as a ten-piece set is a ceiling the design
#: already contains rather than a new number.
#:
#: WITHOUT A CAP THE ANTI-STUN-LOCK RULE STOPS WORKING, and that is arithmetic
#: rather than judgement. The immunity window is 5 seconds. A stun lasting 5
#: seconds or more means the window expires while the target is still held, so
#: the next hit re-stuns immediately and the target never acts again -- which is
#: exactly the "chain-stunned by large hits" the window exists to stop. Blunt's
#: 0.75 second base reaches 5 seconds at 667% chance to stun, and the weakening
#: ailments show that much chance is reachable: affixes alone reach 165% and gems
#: reach 150% each.
#:
#: AT THIS CAP A STUNNED TARGET ALWAYS GETS AT LEAST 2 SECONDS TO ACT, because 3
#: is 2 short of the window. That is the property the cap is for.
LONGEST_STUN_SECONDS = 3.0

#: Chance to stun caps at certainty, and everything past it becomes duration.
#: The same 100 `AILMENT_CHANCE_CAP` uses in `affixes.py`, kept here because this
#: module has no dependency on that one.
STUN_CHANCE_CAP = 100.0


def stun_application(total_chance: float,
                     base_seconds: float = INCIDENTAL_STUN_SECONDS) -> tuple[float, float]:
    """Chance to stun, and how long the stun lasts, from one total chance.

    THE SAME SHAPE `affixes.ailment_application` USES, and deliberately so. The
    project owner stated the rule for damage over time on 2026-08-03 -- "DoT
    chance caps at 100%, anything beyond 100% applies to the magnitude of the
    DoT's effect ... if you have 800% chance to apply it, it gets a 700%
    multiplier" -- and extended it to stun on 2026-08-16. A stun has no damage,
    so its magnitude IS its duration.

    WHY THE OVERFLOW RULE AT ALL. Chance to stun comes from a weapon sub-type, an
    affix, and whatever gems and enchantments grant later, and all of those
    scale. Without it a build stacking them would hit a ceiling at 100% and every
    point past it would be dead, which is what the ailments avoid.

    WHY THIS ONE HAS A HARD STOP AND THE AILMENTS DO NOT. A weakening ailment's
    magnitude caps and then rolls over into duration, so its scaling never dies.
    A stun's magnitude is its duration, so there is nothing to roll over into --
    and duration is the one thing that must not run away, because a stun as long
    as the immunity window is a permanent hold. Past `LONGEST_STUN_SECONDS` the
    extra chance really is dead, and that is the intended outcome rather than an
    oversight.

    Returns the chance to stun, capped at certainty, and the duration in seconds.
    """
    if total_chance < 0.0:
        raise ValueError(f"a chance to stun of {total_chance}% is not a chance")
    if base_seconds < 0.0:
        raise ValueError(f"a stun of {base_seconds} seconds is not a duration")

    applied = min(STUN_CHANCE_CAP, total_chance)
    multiplier = max(1.0, total_chance / STUN_CHANCE_CAP)
    seconds = min(LONGEST_STUN_SECONDS, base_seconds * multiplier)
    return applied, seconds


def can_be_stunned(damage_to_health: float, defender: Defender) -> bool:
    """Whether a hit is even eligible to stun, before any chance roll.

    Two ways to be ineligible: the defender is a boss, or the hit did not take
    enough of the defender's maximum health to count as a heavy blow.

    MEASURED AGAINST DAMAGE ACTUALLY DEALT, not the attack's raw damage. A hit
    that armour and resistance reduced to a scratch is a scratch, which is the
    whole point of the rule -- a well defended character stops being interrupted
    by chip damage. Both surveyed games do the same.

    A DESIGNED STUN IGNORES THE THRESHOLD. See `Attacker.stun_is_designed`.
    """
    if defender.is_boss:
        return False
    if defender.health <= 0.0:
        return False
    return damage_to_health >= defender.health * STUN_DAMAGE_THRESHOLD / 100.0

# --------------------------------------------------------------------------
# Energy shield recharge
# --------------------------------------------------------------------------

#: An energy shield starts refilling 3 seconds after the character last took
#: damage, and taking damage again inside that window restarts the wait.
#:
#: The delay itself was not invented here. `EnchantmentsPositive.csv` line 118 is
#: "Energy shield regeneration begins immediately after taking damage with no
#: delay", which is only worth an enchantment slot if there is normally a delay.
#: The 3 second figure is the project owner's.
ENERGY_SHIELD_RECHARGE_DELAY = 3.0

#: Whether damage over time restarts the wait.
#:
#: Proposed as True, and it is the reason damage over time is the answer to
#: shield stacking. The shield does not absorb damage over time at all, so
#: without this a bleeding character would keep refilling their shield while the
#: bleed runs, and the two rules together would make shields very strong against
#: exactly the damage they ignore.
#:
#: With it, damage over time bypasses the shield AND holds it empty, which is a
#: real counter rather than a stat check.
DAMAGE_OVER_TIME_DELAYS_RECHARGE = True

WEAPON_SUBTYPES = ("Piercing", "Slashing", "Blunt", "Magic", "None")


@dataclass(frozen=True)
class Attacker:
    """What the incoming hit is."""

    damage: float
    #: Which of the eight resistances applies.
    damage_type: str = "Demonic"
    #: Percentage points subtracted from the defender's resistance.
    #: Enchantments already grant this: "Your skills ignore 10%-25% of enemy
    #: resistances", "Your DoTs ignore 20%-40% of enemy resistances".
    penetration: float = 0.0
    #: Percentage of the defender's ARMOR ignored. A separate stat from
    #: resistance penetration, and the enchantment tables treat it separately
    #: too: "Your skills ignore 10%-25% of enemy armor", "Your critical hits
    #: ignore 20%-40% of enemy armor", "Your first hit against each enemy
    #: ignores all armor". Piercing adds its own 20% on top of whatever gear
    #: provides.
    armor_penetration: float = 0.0
    subtype: str = "None"
    #: Area damage cannot be evaded. It can still be blocked.
    is_area: bool = False
    #: Damage over time -- bleed, poison, burn and the rest. Routed differently
    #: from a hit: see the energy shield and mana notes on Defender.
    is_damage_over_time: bool = False
    #: Percentage points of stun chance added by gear, on top of whatever the
    #: weapon sub-type provides. This is the affix family blunt needs in order to
    #: scale, and it does not exist yet: see issue #79.
    bonus_stun_chance: float = 0.0

    #: Whether this attack's stated effect is to stun, rather than the stun being
    #: a side effect of the damage.
    #:
    #: FOUR SHIPPED SKILLS SET THIS, in `game/Data/WeaponSkills.csv`: Shield Bash
    #: stuns for 1.5 seconds, Shockwave Leap for 1, Lunge for 0.75, and Whip
    #: Swing "briefly". So does the Brute's Heart set enchantment, for 3 seconds.
    #:
    #: A DESIGNED STUN IGNORES THE DAMAGE THRESHOLD. Shield Bash's whole purpose
    #: is to stun, so a threshold that made it fail against a healthy target
    #: would leave the skill doing nothing it was written to do. It does NOT
    #: ignore boss immunity, and it does not ignore the immunity window.
    stun_is_designed: bool = False

    def total_stun_chance(self) -> float:
        """Every source of chance to stun added up, uncapped.

        NOT CLAMPED HERE, and that is the point of the split. Everything above
        100% becomes duration rather than being discarded; `stun_application`
        below is what divides it into the two.
        """
        base = BLUNT_STUN_CHANCE if self.subtype == "Blunt" else 0.0
        return max(0.0, base + self.bonus_stun_chance)

    def stun_chance(self) -> float:
        """Chance to stun before the target's crowd control resistance.

        Capped at certainty. What is above it is not lost: see
        `stun_application`.
        """
        return min(STUN_CHANCE_CAP, self.total_stun_chance())

    def __post_init__(self) -> None:
        if self.subtype not in WEAPON_SUBTYPES:
            raise ValueError(f"unknown weapon sub-type {self.subtype!r}")
        if self.damage < 0:
            raise ValueError("damage cannot be negative")

    def total_armor_ignored(self) -> float:
        """Armor bypassed, from the weapon sub-type and from gear together."""
        ignored = self.armor_penetration
        if self.subtype == "Piercing":
            ignored += PIERCING_ARMOR_IGNORED
        return min(100.0, max(0.0, ignored))


@dataclass(frozen=True)
class Defender:
    """The stats a hit is resolved against. Names match the character sheet."""

    health: float
    energy_shield: float = 0.0
    armor: float = 0.0
    evasion: float = 0.0
    block_chance: float = 0.0
    damage_reduction: float = 0.0
    resistances: dict[str, float] = field(default_factory=dict)
    tier: int = 1
    mana: float = 0.0
    #: Reduces the chance of being stunned, as a percentage of that chance.
    crowd_control_resistance: float = 0.0

    #: Whether this defender is a boss. A boss cannot be stunned at all.
    #:
    #: Stated by the project owner 2026-08-05, issue #216. The same rule that
    #: stops the player being stun-locked would otherwise let the player
    #: chain-stun a boss, and a boss that can be held still is not a fight. The
    #: three surveyed games all reach for something here and none of them uses
    #: plain immunity: Path of Exile makes a unique boss immune only while
    #: stunned and for 4 seconds after, Last Epoch counts a boss as having 50%
    #: more health for the stun threshold, and Diablo IV routes crowd control
    #: into a separate stagger meter. Outright immunity is the simplest of the
    #: four and is what was chosen.
    is_boss: bool = False

    # -- What makes energy shield a distinct defence rather than extra health --
    #
    # These defaults are not invented. They are read out of the enchantment
    # tables, where an enchantment that REMOVES a property proves the property
    # exists by default.
    #
    #: Energy shield ignores damage over time. Proven by a NEGATIVE enchantment,
    #: `EnchantmentsNegative.csv` line 165, "Energy shield can now be effected by
    #: bleed" -- which is only a drawback if the shield normally is not.
    shield_absorbs_damage_over_time: bool = False

    #: Damage over time hits mana before health. From a POSITIVE enchantment,
    #: `EnchantmentsPositive.csv` line 202, "DoTs deal damage to your mana pool
    #: first" -- so this is off by default and is a mana-stacking build choice.
    mana_absorbs_damage_over_time: bool = False

    def resistance_to(self, damage_type: str) -> float:
        return self.resistances.get(damage_type, 0.0)


@dataclass(frozen=True)
class Resolution:
    """What happened to one hit, step by step, so it can be inspected."""

    evaded: bool
    blocked: bool
    incoming: float
    after_block: float
    after_armor: float
    after_resistance: float
    after_reduction: float
    absorbed_by_shield: float
    dealt_to_health: float
    absorbed_by_mana: float = 0.0
    stunned: bool = False
    stun_seconds: float = 0.0

    @property
    def total_mitigated(self) -> float:
        return self.incoming - self.dealt_to_health


def armor_reduction(armor: float, tier: int) -> float:
    """Armor as a percentage of damage removed."""
    if armor <= 0:
        return 0.0
    k = ARMOR_CONSTANT_PER_TIER * max(1, tier)
    return min(ARMOR_REDUCTION_CAP, 100.0 * armor / (armor + k))


def effective_resistance(resistance: float, penetration: float) -> float:
    """Resistance after penetration, then capped.

    THE ORDER HERE IS THE MOST LOAD-BEARING CHOICE IN THIS MODULE. Penetration
    is subtracted BEFORE the cap is applied, which is the only thing that makes
    over-capping worth anything: a defender at 100 resistance facing 30
    penetration still sits at the 70 cap, where one at exactly 70 drops to 40.

    Capping first would make every point above 70 worthless and would contradict
    the design's own statement that over-capping is possible via affixes.

    PENETRATION STOPS AT ZERO AND CANNOT PUSH PAST IT. `docs/Cataclysm_GDD_v2.md`
    states it in the enemy resistance subsection of section X: "Penetration beyond
    an enemy's resistance grants no bonus, so over-stacking it does not become a
    damage multiplier against the enemies that need it least." Before issue #482
    the subtraction ran on into negative resistance, so 50 penetration against the
    Abyssal Warden's 35% resistance landed 115% of a hit rather than 100%.

    A NATIVELY NEGATIVE RESISTANCE IS UNTOUCHED, and telling the two cases apart
    is the whole reason the floor below is not simply zero. Several enchantments
    push a target's resistance under zero deliberately and that target should take
    extra damage; what must not happen is penetration MANUFACTURING that state
    against a target that resists. So the floor penetration may reach is the
    lower of zero and the target's own resistance, and `RESISTANCE_FLOOR` still
    bounds how far a natively negative one can go.
    """
    reachable_by_penetration = min(resistance, 0.0)
    penetrated = max(resistance - penetration, reachable_by_penetration)
    return max(RESISTANCE_FLOOR, min(RESISTANCE_CAP, penetrated))


def effective_stun_chance(attacker: Attacker, defender: Defender) -> float:
    """Chance to stun after the defender's crowd control resistance.

    Resistance reduces the chance proportionally rather than subtracting from
    it, so a character at 100 resistance cannot be stunned at all and one at 50
    is stunned half as often, whatever the incoming chance.

    IT REDUCES THE ATTACKER'S TOTAL, NOT THE CAPPED CHANCE, so a defender's
    resistance bites into the overflow as well. A 50 resistance defender facing
    400% chance to stun sees 200%, which is still certainty and still doubles
    the duration -- but sees it from twice as much investment as before. Reducing
    only the capped 100 would make resistance worth nothing at all against a
    heavy stun build, which is the opposite of what a defensive stat is for.
    """
    reduction = max(0.0, min(100.0, defender.crowd_control_resistance))
    return attacker.total_stun_chance() * (1.0 - reduction / 100.0)


def stun_against(attacker: Attacker, defender: Defender) -> tuple[float, float]:
    """The chance to stun this defender and how long the stun would last.

    Both halves after the defender's crowd control resistance, which is the only
    place the two are put together.

    THE DURATION DOES NOT DEPEND ON THE WEAPON. Every source of chance to stun --
    a blunt weapon's 10%, an affix, whatever gems and enchantments grant later --
    fills one pool, and a stun rolled from that pool lasts
    `INCIDENTAL_STUN_SECONDS` before the overflow lengthens it. A skill whose
    STATED effect is to stun is a different thing entirely and carries its own
    duration in its data; see `Attacker.stun_is_designed`.
    """
    return stun_application(effective_stun_chance(attacker, defender))


def resolve(attacker: Attacker, defender: Defender,
            rng: random.Random | None = None,
            force_evade: bool | None = None,
            force_block: bool | None = None,
            force_stun: bool | None = None) -> Resolution:
    """Run one hit through the whole order.

    `force_evade`, `force_block` and `force_stun` exist so tests can pin the
    three random rolls and check the arithmetic rather than the dice.
    """
    rng = rng or random.Random()

    def zero(evaded: bool) -> Resolution:
        return Resolution(evaded=evaded, blocked=False,
                          incoming=attacker.damage, after_block=0.0,
                          after_armor=0.0, after_resistance=0.0,
                          after_reduction=0.0, absorbed_by_shield=0.0,
                          dealt_to_health=0.0)

    # 1. Evasion. Direct attacks only; area damage lands regardless.
    if not attacker.is_area:
        evaded = (force_evade if force_evade is not None
                  else rng.uniform(0, 100) < defender.evasion)
        if evaded:
            return zero(evaded=True)

    # 2. Block. Applies to area damage too. Removes half the hit, not all of it.
    blocked = (force_block if force_block is not None
               else rng.uniform(0, 100) < defender.block_chance)
    damage = attacker.damage
    if blocked:
        damage *= 1.0 - BLOCK_DAMAGE_REDUCTION / 100.0
    after_block = damage

    # 3. Armor, after whatever share of it the attacker ignores.
    armor = defender.armor * (1.0 - attacker.total_armor_ignored() / 100.0)
    damage *= 1.0 - armor_reduction(armor, defender.tier) / 100.0
    after_armor = damage

    # 4. Resistance, penetrated first and capped second.
    resist = effective_resistance(
        defender.resistance_to(attacker.damage_type), attacker.penetration)
    damage *= 1.0 - resist / 100.0
    after_resistance = damage

    # 5. Flat damage reduction.
    damage *= 1.0 - defender.damage_reduction / 100.0
    after_reduction = damage

    # 6. Mana, but only for damage over time and only if the character has built
    # for it. From "DoTs deal damage to your mana pool first".
    absorbed_by_mana = 0.0
    if attacker.is_damage_over_time and defender.mana_absorbs_damage_over_time:
        absorbed_by_mana = min(defender.mana, damage)
        damage -= absorbed_by_mana

    # 7. Energy shield. It ignores damage over time unless an enchantment has
    # taken that immunity away, which is what makes it a distinct defence rather
    # than a second health bar. Magic weapons strip more of it per hit.
    shield_applies = (not attacker.is_damage_over_time
                      or defender.shield_absorbs_damage_over_time)
    magic = 1.0 + SUBTYPE_BONUS / 100.0 if attacker.subtype == "Magic" else 1.0

    absorbed = 0.0
    consumed = 0.0
    if shield_applies:
        absorbed = min(defender.energy_shield, damage * magic)
        # Convert what the shield actually stopped back into raw damage, so a
        # magic bonus does not destroy more raw damage than the hit contained.
        consumed = absorbed / magic

    to_health = max(0.0, damage - consumed)
    if attacker.subtype == "Slashing":
        to_health *= 1.0 + SUBTYPE_BONUS / 100.0

    # 8. Stun, rolled separately from damage. A hit that is evaded never gets
    # here; a hit that is blocked still can, because a block reduces damage
    # rather than preventing contact.
    #
    # The anti-stun-lock rule gates the roll rather than reducing it. A hit that
    # took too little of the target's maximum health cannot stun however high the
    # attacker's chance is, and a boss cannot be stunned at all. A skill whose
    # stated effect is to stun skips the damage threshold but not boss immunity.
    # Issue #216.
    if defender.is_boss:
        stunned = False
    elif attacker.stun_is_designed or can_be_stunned(to_health, defender):
        stun_chance, stun_seconds = stun_against(attacker, defender)
        stunned = (force_stun if force_stun is not None
                   else rng.uniform(0, 100) < stun_chance)
    else:
        stunned = False
        stun_seconds = 0.0

    return Resolution(
        evaded=False, blocked=blocked, incoming=attacker.damage,
        after_block=after_block, after_armor=after_armor,
        after_resistance=after_resistance, after_reduction=after_reduction,
        absorbed_by_shield=absorbed, absorbed_by_mana=absorbed_by_mana,
        dealt_to_health=min(to_health, defender.health),
        stunned=stunned,
        stun_seconds=stun_seconds if stunned else 0.0,
    )


def restarts_recharge(attacker: Attacker) -> bool:
    """Whether taking this hit restarts the energy shield's wait."""
    if attacker.is_damage_over_time:
        return DAMAGE_OVER_TIME_DELAYS_RECHARGE
    return True


def shield_after_quiet_period(current_shield: float, max_shield: float,
                              regen_per_second: float, seconds_since_damage: float,
                              delay: float = ENERGY_SHIELD_RECHARGE_DELAY) -> float:
    """The shield after a stretch of time without taking damage.

    Nothing happens until the delay has fully elapsed. Any damage inside the
    window means the caller starts this over from zero, which is what "unless
    you receive damage again in that time" means.
    """
    if seconds_since_damage <= delay:
        return min(current_shield, max_shield)
    recharging_for = seconds_since_damage - delay
    return min(max_shield, current_shield + regen_per_second * recharging_for)


def shield_over_timeline(max_shield: float, regen_per_second: float,
                         events: list[tuple[float, float]],
                         delay: float = ENERGY_SHIELD_RECHARGE_DELAY,
                         ends_at: float | None = None) -> float:
    """Shield remaining after a series of (time in seconds, damage) events.

    Exists to prove the restart rule rather than only the delay: a character hit
    every 2 seconds never recharges at all, while the same total damage spaced 4
    seconds apart does.

    Events must be in time order. Damage is applied straight to the shield, so
    this models the shield's own behaviour and not the whole mitigation order.
    """
    shield = max_shield
    last_damage_at = None
    for at, amount in events:
        if last_damage_at is not None:
            shield = shield_after_quiet_period(
                shield, max_shield, regen_per_second, at - last_damage_at, delay)
        shield = max(0.0, shield - amount)
        last_damage_at = at

    if ends_at is not None and last_damage_at is not None:
        shield = shield_after_quiet_period(
            shield, max_shield, regen_per_second, ends_at - last_damage_at, delay)
    return shield


def hits_to_kill(attacker: Attacker, defender: Defender,
                 max_hits: int = 10_000) -> float:
    """How many hits it takes to empty the shield and then the health.

    Applied one hit at a time, with the shield depleting as it absorbs. Dividing
    the combined pool by the damage of a single hit would be wrong: it assumes
    the shield absorbs its whole value again on every hit, when a shield is
    spent once. That mistake makes a shielded character look several times
    tougher than it is.

    Regeneration is deliberately not modelled here. Between hits a character
    does regain shield, but how much depends on the interval between hits, and
    there are no attack speed numbers to supply it.

    THE HEALTH PASSED TO `resolve` IS THE FULL POOL, NOT WHAT IS LEFT. `resolve`
    clamps a hit to the health remaining, because it reports what one hit
    actually dealt and a hit cannot deal more damage than there was health. That
    clamp is wrong to apply inside this loop: it makes the last hits look
    smaller than they are, and `average_damage_taken` then averages the clamped
    figures, so the running total creeps toward zero instead of crossing it.

    The effect was a phantom extra hit on every count. Against the reference
    build a Cataclysm Boss landing 6,635 on 11,023 health reported 3 hits when
    the answer is 2. The shield state below is still tracked hit by hit, because
    a shield genuinely is spent once and that is the whole reason this function
    is a loop rather than a division.
    """
    shield = defender.energy_shield
    health = defender.health
    for hit in range(1, max_hits + 1):
        state = Defender(
            health=defender.health, energy_shield=shield, armor=defender.armor,
            evasion=defender.evasion, block_chance=defender.block_chance,
            damage_reduction=defender.damage_reduction,
            resistances=defender.resistances, tier=defender.tier)
        taken = average_damage_taken(attacker, state)
        # Work out how much of the shield that average hit consumed.
        blocked = resolve(attacker, state, force_evade=False, force_block=True)
        unblocked = resolve(attacker, state, force_evade=False, force_block=False)
        bc = min(100.0, defender.block_chance) / 100.0
        absorbed = bc * blocked.absorbed_by_shield + (1 - bc) * unblocked.absorbed_by_shield
        if not attacker.is_area:
            absorbed *= 1.0 - min(100.0, defender.evasion) / 100.0

        if taken <= 0 and absorbed <= 0:
            return float("inf")
        shield = max(0.0, shield - absorbed)
        health -= taken
        if health <= 0:
            return float(hit)
    return float("inf")


def average_damage_taken(attacker: Attacker, defender: Defender) -> float:
    """Expected damage to health, averaging over the evasion and block rolls.

    Useful for comparing builds without sampling thousands of hits.
    """
    evade_chance = 0.0 if attacker.is_area else min(100.0, defender.evasion) / 100.0
    block_chance = min(100.0, defender.block_chance) / 100.0

    blocked = resolve(attacker, defender, force_evade=False, force_block=True)
    unblocked = resolve(attacker, defender, force_evade=False, force_block=False)
    hit = (block_chance * blocked.dealt_to_health
           + (1.0 - block_chance) * unblocked.dealt_to_health)
    return (1.0 - evade_chance) * hit


if __name__ == "__main__":
    from .character import Attributes, Character
    from .classes import DEMONIC_CLASSES

    print("Proposed damage calculation, issue #93")
    print()
    print("  Order: evasion, block, armor, resistance, flat reduction,")
    print("  energy shield, health.")
    print()
    print("  Armor as a percentage, by tier. K rises 800 per tier, so armor")
    print("  earned early does not keep its value forever:")
    print()
    print(f"    {'armor':>7} " + "".join(f"{'T' + str(t):>7}" for t in (1, 4, 8)))
    for armor in (100, 371, 1000, 3000, 8000):
        row = f"    {armor:>7} "
        for tier in (1, 4, 8):
            row += f"{armor_reduction(armor, tier):>6.1f}%"
        print(row)
    print()

    print("  Why penetration is applied BEFORE the 70% cap. Two defenders,")
    print("  one at the cap and one over-capped, against 30 penetration:")
    print()
    for raw in (70.0, 100.0):
        capped_first = min(RESISTANCE_CAP, raw) - 30.0
        pen_first = effective_resistance(raw, 30.0)
        print(f"    {raw:>5.0f} resistance -> cap first: {capped_first:>5.1f}%   "
              f"penetrate first: {pen_first:>5.1f}%  <- proposed")
    print()
    print("    Capping first makes every point above 70 worthless, which")
    print("    contradicts the design allowing affixes to over-cap.")
    print()

    print("  A hit of 1,000 Demonic damage against each Demonic class at")
    print("  level 100, no gear, every point in Vitality, tier 8:")
    print()
    print(f"    {'class':<11} {'health':>8} {'shield':>7} {'armor':>7} "
          f"{'taken':>7} {'hits to kill':>13}")
    for name, definition in DEMONIC_CLASSES.items():
        c = Character(definition, level=100, attributes=Attributes(vitality=100))
        d = Defender(
            health=c.stat("max_health"),
            energy_shield=c.stat("max_energy_shield"),
            armor=c.stat("armor"),
            evasion=c.stat("evasion"),
            block_chance=c.stat("block_chance"),
            damage_reduction=c.stat("damage_reduction"),
            resistances={"Demonic": c.stat("resistance_demonic")},
            tier=8,
        )
        attack = Attacker(damage=1000.0)
        taken = average_damage_taken(attack, d)
        print(f"    {name:<11} {d.health:>8,.0f} {d.energy_shield:>7,.0f} "
              f"{d.armor:>7,.0f} {taken:>7,.0f} "
              f"{hits_to_kill(attack, d):>13.0f}")
    print()
    print("  Hits to kill applies hits one at a time with the shield depleting.")
    print("  Dividing the combined pool by one hit's damage would be wrong: it")
    print("  assumes the shield absorbs its whole value again on every hit, and")
    print("  it made the Ritualist look three times tougher than it is.")
    print()
    print("  Energy shield and armor both do something here, which they do not")
    print("  in the game today, because nothing reads them.")
