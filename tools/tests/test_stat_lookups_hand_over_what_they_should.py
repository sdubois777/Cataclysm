"""Every place that asks for a stat is listed here, with what it hands over.

WHY THIS EXISTS. Issue #1992. `UCataclysmAbilitySystemComponent::StatForSkill`
takes the state a condition may read as POSITIONAL PARAMETERS WITH DEFAULTS --
the target is the ninth, the distance to it the seventh, whether it is staggered
the eighth. A call that stops early supplies nothing for the rest, **and nothing
at that call site is written down to be wrong.** The condition then reads a field
nobody filled, answers false every time, and the row grants nothing, with no
error anywhere. That has happened three times: issues #1973, #1981 and #1982.

WHAT THIS DOES NOT DO, AND CANNOT. It does not find the faults. Measured on
af715829: of the 61 call sites, 54 pass three arguments and supply no state
at all -- and nearly all of those are CORRECT, because there is no blow, no
target and no skill in hand at a regeneration tick or a resource generator.
Fifteen are in `CataclysmFervour.cpp` alone. A check that flagged every short
call would report dozens of problems, be wrong about almost all of them, and be
switched off.

SO IT MAKES THE DECISION VISIBLE INSTEAD. `INVENTORY` records every call site
and what it hands over, each with one short reason, so that a three-argument call
in a regeneration tick is recorded as CORRECT rather than merely tolerated. A new
call site, a changed one, or one that has gone all fail it, which forces whoever
adds or edits a lookup to write down what state it passes.

AND ONE THING IS ASSERTED OUTRIGHT. A call that hands over a target must hand
over the distance and the stagger too, or be listed by name in
`TARGET_WITHOUT_THE_DISTANCE_OR_THE_STAGGER` with the reason. That is issue
#1992's own finding. It caught the change made for #1982, which passed a target
and left both at their defaults; those two lookups were that list's only entries,
and the list emptied when #1992 handed them the distance and the stagger.

HOW IT READS THE SOURCE, said here because a check should report its own scope.
Comments and string literals are blanked first, so a name in prose or inside
`TEXT("...")` is never counted; the definition of the function itself is skipped;
arguments are split by MATCHING PARENTHESES rather than on commas, because
`FCataclysmBlowContext()` and `AbilitySystem->GetNumericAttribute(Speed)` hold
commas and brackets of their own. Only `game/Source` is read, and anything under
a `Tests` directory is excluded.

THE KEY IS THE FILE AND THE WHOLE ARGUMENT LIST. Not the line number, which goes
stale on any edit above it; not the stat name, because 11 of the 61 name
their stat literally and the rest pass a variable. Measured: the whole argument
list gives 61 distinct keys for 61 calls, with none colliding, where the file
and the first argument alone collide on two calls in `CataclysmRegeneration.cpp`.

THE LINES HERE ARE LONG ON PURPOSE. An argument list is data rather than prose,
and splitting one across fragments would bury a real change in reflowing. This
project does not enforce a line length; `pyproject.toml` selects `E9`, `F` and
`B` only, and says why.
"""

from __future__ import annotations

import pathlib
import re

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / "game" / "Source"

#: The commit the counts below were measured on. A count is a measurement of a
#: tree rather than a property of the design, so it is labelled with the tree.
MEASURED_AT = "af715829da8789c5f29f19413255d13b0f42f703"

#: How many call sites that commit held. THE INVENTORY BELOW HOLDS 69, counted
#: with `len(INVENTORY)` on 2026-09-23 on branch `feat/zone-first-sweep`,
#: and the extra are dated rather than folded into the figure above. Among
#: them: issue #2000 replaced a rate lookup here with an asked-for reduction,
#: the Behind the Veil keystone added one of its own, issue #1815's live
#: maximum health added one, and issue #1686's non-critical share, projectile
#: later-hit share and zone first-sweep share one each. THIS
#: SENTENCE SAID 64 WHILE THE LIST HELD 66: a count carried forward by adding
#: one keeps an earlier miscount. A count describes a tree, so the label
#: stays with the tree it was taken on and the movement is written out.
CALL_SITES = 63

#: A call site this file must find. THE CONTROL: if the reader breaks, every
#: name looks unused, the inventory looks complete, and nothing below means
#: anything.
A_CALL_SITE_THAT_HANDS_OVER_EVERYTHING = (
    "game/Source/Cataclysm/AbilitySystem/CataclysmSkillEffects.cpp")

#: A comment in the same file, used to prove the stripping happened at all.
A_COMMENT_IN_THAT_FILE = "NEGATIVE IS CLAMPED AWAY FOR THE SAME REASON"

#: What each optional parameter is when the caller says nothing. Positions are
#: zero-based into the argument list.
DEFAULTS = {3: "-1.0f", 4: "FCataclysmBlowContext()", 5: "-1.0f",
            6: "-1.0f", 7: "false", 8: "nullptr", 9: "-1"}
DISTANCE, STAGGER, TARGET = 6, 7, 8


def code_only(text: str) -> str:
    """`text` with comments and string literals blanked, same length.

    THE SAME LENGTH, so an offset into the result is an offset into the source.

    NOT BY LINE PREFIX. A wrapped expression can continue on a line that opens
    with a star, so "the line starts with `*`" is not the same question as
    "this is inside a comment".
    """
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        pair = text[i:i + 2]
        if pair == "//":
            j = text.find("\n", i)
            end = n if j < 0 else j
            out.append(" " * (end - i))
            i = end
        elif pair == "/*":
            j = text.find("*/", i + 2)
            end = n if j < 0 else j + 2
            out.append(" " * (end - i))
            i = end
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:min(j + 1, n)])
            i = min(j + 1, n)
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def arguments_of(code: str, at: int) -> list[str] | None:
    """The call's arguments, split by matching parentheses and whitespace-normalised."""
    opened = code.index("(", at)
    depth, start, args = 0, opened + 1, []
    for k in range(opened, len(code)):
        c = code[k]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append(code[start:k])
                return [" ".join(a.split()) for a in args]
        elif c == "," and depth == 1:
            args.append(code[start:k])
            start = k + 1
    return None


def call_sites() -> dict[tuple[str, str], list[str]]:
    """Every non-test call, keyed by file and whole argument list."""
    found: dict[tuple[str, str], list[str]] = {}
    for path in sorted(SOURCE_ROOT.rglob("*.cpp")):
        if "Tests" in path.parts:
            continue
        code = code_only(path.read_text(encoding="utf-8", errors="replace"))
        for m in re.finditer(r"(?<![A-Za-z_])StatForSkill\s*\(", code):
            if code[max(0, m.start() - 2):m.start()].endswith("::"):
                continue          # the definition, not a call
            args = arguments_of(code, m.start())
            if args is None:
                continue
            key = (path.relative_to(REPO_ROOT).as_posix(), ", ".join(args))
            found[key] = args
    return found


INVENTORY = {
    ('game/Source/Cataclysm/AbilitySystem/CataclysmAbilitySystemComponent.cpp',
     'Stat, FGameplayTagContainer(), GetNumericAttributeBase(MaxHealth)'):
        "maximum health worked out again with the character's state now, so a "
        'row sized by the minions held reaches the attribute; no tags, because '
        'the fold it corrects had none, and no blow is in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmCommand.cpp',
     'FName(Stat), FGameplayTagContainer(), 0.0f'):
        "the two numbers Behind the Veil states -- how far a Ritualist's "
        'minions draw nearby enemies off their summoner, and how many '
        'minions that takes -- asked of the character a creature would '
        'otherwise attack. No tags, because both are properties of that '
        'character and not of any skill, and no blow is in hand: the '
        'question is asked while a creature chooses whom to attack, '
        'before anything is thrown',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmAilments.cpp',
     'FName(Kind.Stat), SkillTags, FromAttribute, SkillHealthCostPercent'):
        "an ailment's chance and magnitude, asked with the skill's tags "
        'so a row scoped to a kind of skill reaches it; no blow is in '
        'hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmAilments.cpp',
     'FName(Kind.MagnitudeStat), SkillTags, Held, SkillHealthCostPercent'):
        "an ailment's chance and magnitude, asked with the skill's tags "
        'so a row scoped to a kind of skill reaches it; no blow is in '
        'hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmBasicAttack.cpp',
     'FName(TEXT("attack_speed")), FGameplayTagContainer(), AbilitySystem->GetNumericAttribute(Speed)'):
        "the basic attack's own speed, asked before a blow is thrown",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmContagion.cpp',
     'FName(AuraDurationStat), FGameplayTagContainer(), 0.0f'):
        "an aura's duration and chance, asked of the character rather "
        'than of a blow',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmContagion.cpp',
     'FName(TormentChanceStat), FGameplayTagContainer(), 0.0f'):
        "an aura's duration and chance, asked of the character rather "
        'than of a blow',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmContagion.cpp',
     'FName(DeathChanceStat), FGameplayTagContainer(), 0.0f'):
        "an aura's duration and chance, asked of the character rather "
        'than of a blow',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmDamageCalculation.cpp',
     'FName(Stat), FGameplayTagContainer(), FromAttribute, -1.0f, Blow'):
        "the defender's own stats, asked WITH THE BLOW so a row about "
        'the hit being melee, ranged or a spell can be judged',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmDamageConversion.cpp',
     'FName(UCataclysmDamageConversion::ActiveStat), FGameplayTagContainer(), System->GetNumericAttribute(Flag)'):
        'how much of one damage type becomes another, a property of the '
        'attacker',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmDebuffs.cpp',
     'FName(SharedDebuffDamageStat), FGameplayTagContainer(), Source->GetNumericAttribute(Attribute)'):
        'what a debuff this character carries is worth, with no blow in '
        'hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmDebuffs.cpp',
     'FName(DurationStat), FGameplayTagContainer(), Held'):
        'what a debuff this character carries is worth, with no blow in '
        'hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmDebuffs.cpp',
     'FName(DoNotExpireStat), FGameplayTagContainer(), 0.0f'):
        'what a debuff this character carries is worth, with no blow in '
        'hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'Stat, Context, Plain'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(LossSuppressedStat), Healing, 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(PerSecondStat), FGameplayTagContainer(), 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(FromMinionsStat), FGameplayTagContainer(), 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(PerEnemyInReachStat), FGameplayTagContainer(), 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(DecayPerSecondStat), FGameplayTagContainer(), 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(DecayGraceMetresStat), FGameplayTagContainer(), 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(PerCastStat), FGameplayTagContainer(), 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(OnDroppingLowStat), FGameplayTagContainer(), Resource->GetFervourOnDroppingLow()'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(HealthRestoredOnKillStat), FGameplayTagContainer(), 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(HealthRestoredOnKillAtNoCostStat), FGameplayTagContainer(), Resource->GetHealthRestoredOnKillAtNoCost()'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(IncreasedDamageBoughtPerExtraEnemyHitStat), SkillTags, 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(PerEnemyHitStat), SkillTags, 0.0f'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(OnMinionDeathStat), FGameplayTagContainer(), Resource->GetFervourOnMinionDeath()'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(OnEnemyDeathNearbyStat), FGameplayTagContainer(), Resource->GetFervourOnEnemyDeathNearby()'):
        'class resource generation, asked on a timer with no blow, '
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmGameplayAbility.cpp',
     'FName(TEXT("cooldown_reduction")), SkillTags, AbilitySystem->GetNumericAttribute(Reduction)'):
        'the cooldown reduction, asked with the tags of the skill so a row '
        'scoped to a slot or a keyword reaches it; the attribute is the '
        'fallback for a character with no recorded rows. Issue #2000 '
        'replaced a rate lookup here that read the wrong bucket.',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmGameplayAbility.cpp',
     'FName(UCataclysmSkillSlots::CooldownLengtheningStat), SkillTags, 0.0f'):
        'how much longer the cooldown is, asked with the tags of the skill so '
        'a row scoped to a slot reaches only that slot; nought is the '
        'fallback because the stat has no attribute. Issue #1994.',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmGameplayAbility.cpp',
     'FName(TEXT("cooldown_skip_chance")), FGameplayTagContainer(), 0.0f'):
        'the chance a skill does not go on cooldown, asked of the '
        'character',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmGameplayAbility.cpp',
     'FName(ManaCostAsCurrentHealthPercentStat), SkillTagsForStats(), 0.0f'):
        'the share of current health a cast pays instead of its mana, asked '
        'with the tags of the skill; its only row carries mana_below, which '
        'reads the caster\'s own mana, so no blow or target is needed; nought '
        'is the fallback because the stat has no attribute. Issues #1820 and '
        '#41.',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmHealthDebt.cpp',
     'FName(UnpayableBecomesDebtStat), FGameplayTagContainer(), AbilitySystem->GetNumericAttribute(Flag)'):
        "the health debt's own rates, read off the character",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmHealthDebt.cpp',
     'FName(ClearedOnDroppingLowStat), FGameplayTagContainer(), AbilitySystem->GetNumericAttribute(Flag)'):
        "the health debt's own rates, read off the character",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmLeech.cpp',
     'FName(Stat), FGameplayTagContainer(), FromAttribute'):
        'how much a leech returns, asked of the character after the '
        'blow has already resolved',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmLeech.cpp',
     'FName(TEXT("life_leech")), FGameplayTagContainer(), Vitals->GetLifeLeech()'):
        'how much a leech returns, asked of the character after the '
        'blow has already resolved',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmLowHealthRelief.cpp',
     'FName(UCataclysmHealthDebt::ClearedOnDroppingLowStat), NoTags, AbilitySystem->GetNumericAttribute(Cleared)'):
        "a relief that reads only the character's own health",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmLowHealthRelief.cpp',
     'FName(UCataclysmFervour::OnDroppingLowStat), NoTags, AbilitySystem->GetNumericAttribute(Fervour)'):
        "a relief that reads only the character's own health",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmMinion.cpp',
     'FName(Stat), FGameplayTagContainer(), 0.0f'):
        "a minion's own health and damage, asked with an empty tag "
        'container',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmNova.cpp',
     'FName(DamageStat), FGameplayTagContainer(), 0.0f'):
        "a nova's own figures, asked before it strikes anything",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmProjectile.cpp',
     'FName(UCataclysmDamageCalculation::ProjectileLaterHitDamageStat), SkillTags, UCataclysmDamageCalculation::NormalProjectileLaterHitDamage'):
        "the share a projectile's later contact keeps (issue #1686), with "
        "the firing skill's tags; the count of contacts is the "
        "projectile's own, so no blow state is handed over",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmRegeneration.cpp',
     'FName(Stat), FGameplayTagContainer(), Stored'):
        'a regeneration tick, which is a clock rather than a blow',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmRegeneration.cpp',
     'FName(Stat), FGameplayTagContainer(), AbilitySystem->GetNumericAttribute( Attribute)'):
        'a regeneration tick, which is a clock rather than a blow',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmRetaliation.cpp',
     'FName(Stat), FGameplayTagContainer(), Held'):
        "retaliation's own figures, read off the character that was hit",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillEffects.cpp',
     'FName(TEXT("spell_damage")), SkillTags, FromAttribute, SkillHealthCostPercent, FCataclysmBlowContext(), -1.0f, TargetDistanceMetres, bTargetIsStaggered, Target, EnemiesStruckTogether'):
        "the attacker's damage for one blow, and the only call that "
        'hands over the whole blow: distance, stagger, target and the '
        'group struck',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillEffects.cpp',
     'StatName, SkillTags, FromAttribute'):
        'a figure read off the attacker with no blow in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillEffects.cpp',
     'FName(Stat), FGameplayTagContainer(), Held'):
        'a figure read off the attacker with no blow in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillEffects.cpp',
     'FName(CrowdControlResistanceStat), FGameplayTagContainer(), Stat'):
        'a figure read off the attacker with no blow in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp',
     'FName(UCataclysmDamageCalculation::ZoneFirstSweepDamageStat), SkillTags, UCataclysmDamageCalculation::NormalZoneFirstSweepDamage'):
        "the share a skill's zone deals on its first sweep (issue #1686), "
        "asked where the zone is priced; a zone is not a blow",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp',
     'FName(UCataclysmSkillSlots::LockedStat), SkillTags, 0.0f'):
        "a skill's own cost and shape, asked before any blow exists",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp',
     'FName(UCataclysmSkillEffects::KnockdownSecondsStat), SkillTags, 0.0f'):
        "a skill's own cost and shape, asked before any blow exists",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp',
     'FName(TEXT("added_health_cost")), SkillTags, FromAttribute'):
        "a skill's own cost and shape, asked before any blow exists",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp',
     'FName(MeleeReachMetresStat), SkillTags, 0.0f'):
        "a strike's own reach, asked with the skill's tags before any blow "
        'exists',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp',
     'FName(HealthCostSuppressedStat), FGameplayTagContainer(), 0.0f'):
        "a skill's own cost and shape, asked before any blow exists",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp',
     'FName(ManaPoolBecomesHealthStat), FGameplayTagContainer(), 0.0f'):
        "a skill's own cost and shape, asked before any blow exists",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplate.cpp',
     'FName(TEXT("added_health_cost_of_current")), SkillTags, FromAttribute'):
        "a skill's own cost and shape, asked before any blow exists",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplates.cpp',
     'FName(StatName), SkillTags, Mine->GetNumericAttribute(Stat)'):
        "an aura's upkeep and a skill's own numbers, asked with no blow "
        'in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplates.cpp',
     'FName(UCataclysmCommand::PossessionThresholdBonusStat), SkillTags, Mine->GetNumericAttribute(Stat)'):
        "an aura's upkeep and a skill's own numbers, asked with no blow "
        'in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmSkillTemplates.cpp',
     'FName(bForExplosion ? ReplacedOnExplosionStat : ReplacedOnDeathStat), FGameplayTagContainer(), 0.0f'):
        "the seconds between replacements of a lost minion, a flag read off "
        'the commander after a death, with no blow or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmStacks.cpp',
     'FName(CarnageFromDamageTakenStat), FGameplayTagContainer(), AbilitySystem->GetNumericAttribute(Flag)'):
        'how long a stack lasts and how many may be held, read off the '
        'character',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmStacks.cpp',
     'FName(CarnageHasNoMaximumStat), FGameplayTagContainer(), AbilitySystem->GetNumericAttribute(Flag)'):
        'how long a stack lasts and how many may be held, read off the '
        'character',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp',
     'FName(UCataclysmDamageCalculation::MeleeEvasionSuppressedStat), AssetTags, Swinging->GetNumericAttribute(Suppressed)'):
        'a defender reading with no blow in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp',
     'FName(TEXT("penetration")), AssetTags, Offence->GetPenetration()'):
        'a defender reading with no blow in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp',
     'SurvivedEveryStat, FGameplayTagContainer(), 0.0f'):
        "Nothing Stops It's interval, a flag read off the defender when a hit "
        'would kill, with no skill of its own in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmRegeneration.cpp',
     'FName(SharedBloodStat), FGameplayTagContainer(), 0.0f'):
        "Shared Blood's share, asked of a minion's summoner in the minion's "
        'regeneration step with no blow, target or skill in hand',
    ('game/Source/Cataclysm/Items/CataclysmEquipmentComponent.cpp',
     'FName(BothHandsFullStat), FGameplayTagContainer(), 0.0f'):
        "Both Hands Full's flag, asked of the wearer when a weapon is put on "
        'and after its passive allocation changes; no blow or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFollowThrough.cpp',
     'FName(UCataclysmFollowThrough::EverySecondsStat), FGameplayTagContainer(), 0.0f'):
        "Follow Through's interval, asked of the killer after a kill and before "
        'its repeat; a clock, not a blow modifier',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmDebuffs.cpp',
     'FName(NowhereToRunMetresStat), FGameplayTagContainer(), 0.0f'):
        "Nowhere to Run's radius, asked of the holder in its step; a radius, "
        'not a blow modifier',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmFervour.cpp',
     'FName(UCataclysmDebuffs::NowhereToRunMetresStat), FGameplayTagContainer(), 0.0f'):
        "Nowhere to Run's radius as a no-decay radius, asked in the decay step "
        'with no blow in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmShoulderThrough.cpp',
     'FName(Stat), FGameplayTagContainer(), 0.0f'):
        "Shoulder Through's switch, asked of the walker every frame it walks, "
        'before any enemy is found; whether it is held, not a blow modifier',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmAbilitySystemComponent.cpp',
     'FName(MitigatedAddedCapStat), FGameplayTagContainer(), 0.0f'):
        "Nothing Wasted's cap, asked of the holder as damage is stored and as "
        'a melee hit spends it; a share of the hit, not a blow modifier',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmAbilitySystemComponent.cpp',
     'FName(RendPercentStat), FGameplayTagContainer(), 0.0f'):
        "Rendering Blows' share, asked of the striker as a count of landed "
        'melee hits is kept; a flag-like value, not a blow modifier',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmAbilitySystemComponent.cpp',
     'FName(RendSecondsStat), FGameplayTagContainer(), 0.0f'):
        "Rendering Blows' seconds, asked of the striker on the third landed "
        'melee hit; a duration, not a blow modifier',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmDebuffs.cpp',
     'FName(GroundDownMetresStat), FGameplayTagContainer(), 0.0f'):
        "Ground Down's radius, read in a regeneration step with no blow, "
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmDebuffs.cpp',
     'FName(GroundDownPercentStat), FGameplayTagContainer(), 0.0f'):
        "Ground Down's share of a creature's speeds, read in the same step",
    ('game/Source/Cataclysm/AbilitySystem/CataclysmDebuffs.cpp',
     'FName(AppliedHeldWithinMetresStat), FGameplayTagContainer(), 0.0f'):
        "No Second Wind's radius, read in a regeneration step with no blow, "
        'target or skill in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmGameplayAbility.cpp',
     'FName(CostPaidFromEnergyShieldStat), FGameplayTagContainer(), 0.0f'):
        "Cast from Ward's flag, asked when a cost is weighed against the pools, "
        'which is a question about the character and not about one skill',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp',
     'WardStat, FGameplayTagContainer(), 0.0f'):
        "Sacrificial Ward's interval, a flag read off the defender when a blow "
        'would break its energy shield, with no skill of its own in hand',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp',
     'ImmuneForStat, FGameplayTagContainer(), 0.0f'):
        "Nothing Stops It's no-damage seconds, read off the defender once a "
        'lethal hit is survived',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp',
     'FName(TEXT("armor_penetration")), AssetTags, Offence->GetArmorPenetration(), -1.0f, FCataclysmBlowContext(), -1.0f, Hit.OpponentDistanceMetres, UCataclysmSkillEffects::IsStaggered(GetOwningActor()), GetOwningActor(), EnemiesStruckTogether'):
        'armour penetration, handed the whole blow since issue #1992: '
        'the character struck, the distance to it, its stagger and '
        'the count of enemies struck together',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp',
     'FName(TEXT("crit_chance")), AssetTags, Offence->GetCritChance(), -1.0f, FCataclysmBlowContext(), -1.0f, Hit.OpponentDistanceMetres, UCataclysmSkillEffects::IsStaggered( GetOwningActor()), GetOwningActor()'):
        'a critical strike stat, handed the whole blow since issue '
        '#1992: the character struck (issue #1982), the distance to '
        'it and its stagger',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp',
     'FName(TEXT("crit_multiplier")), AssetTags, Offence->GetCritMultiplier(), -1.0f, FCataclysmBlowContext(), -1.0f, Hit.OpponentDistanceMetres, UCataclysmSkillEffects::IsStaggered( GetOwningActor()), GetOwningActor()'):
        'a critical strike stat, handed the whole blow since issue '
        '#1992: the character struck (issue #1982), the distance to '
        'it and its stagger',
    ('game/Source/Cataclysm/AbilitySystem/CataclysmVitalAttributeSet.cpp',
     'FName(UCataclysmDamageCalculation::NonCriticalDamageStat), AssetTags, UCataclysmDamageCalculation::NormalNonCriticalDamage, -1.0f, FCataclysmBlowContext(), -1.0f, Hit.OpponentDistanceMetres, UCataclysmSkillEffects::IsStaggered(GetOwningActor()), GetOwningActor()'):
        'the share a hit keeps when its critical roll fails, asked on '
        'the same terms as the critical multiplier beside it (issue #1686)',
    ('game/Source/Cataclysm/Character/CataclysmPlayerCharacter.cpp',
     'FName(CrowdControlEndsWhenItsApplierDiesStat), FGameplayTagContainer(), Cataclysm->GetNumericAttribute( UCataclysmCombatAttributeSet:: GetCrowdControlEndsWhenItsApplierDiesAttribute())'):
        'a character sheet reading, which has no skill or target in '
        'hand at all',
    ('game/Source/Cataclysm/Character/CataclysmPlayerCharacter.cpp',
     'FName(TEXT("movement_speed")), FGameplayTagContainer(), FromAttribute'):
        'a character sheet reading, which has no skill or target in '
        'hand at all',
    ('game/Source/Cataclysm/Character/CataclysmPlayerCharacter.cpp',
     'FName(MovementSpeedReductionSuppressedStat), FGameplayTagContainer(), AbilitySystem->GetNumericAttribute( UCataclysmCombatAttributeSet:: GetMovementSpeedReductionSuppressedAttribute())'):
        'a character sheet reading, which has no skill or target in '
        'hand at all',
    ('game/Source/Cataclysm/Interface/CataclysmSkillBar.cpp',
     'FName(UCataclysmSkillSlots::LockedStat), Skill->SkillTags, 0.0f'):
        'what the skill bar shows, which has no blow in hand',
}

#: EMPTY SINCE ISSUE #1992. The two critical strike lookups were its only
#: entries, and both now hand over the distance and the stagger.
TARGET_WITHOUT_THE_DISTANCE_OR_THE_STAGGER: dict[tuple[str, str], str] = {}


def test_the_reader_finds_the_call_sites_it_is_meant_to() -> None:
    """The control. A broken reader makes every check below vacuous."""
    found = call_sites()
    assert found, "no call site was found at all; the reader is broken"
    files = {file for file, _ in found}
    assert A_CALL_SITE_THAT_HANDS_OVER_EVERYTHING in files, (
        f"{A_CALL_SITE_THAT_HANDS_OVER_EVERYTHING} holds the one call that "
        "hands over the whole blow and was not found, so the reader is broken")
    assert any(len(args) >= 9 for args in found.values()), (
        "no call was read as passing nine arguments, so the argument split is "
        "broken and every check below would pass on nothing")


def test_the_stripping_of_comments_actually_happens() -> None:
    """Without this, a name in a comment would count as a call."""
    path = REPO_ROOT / A_CALL_SITE_THAT_HANDS_OVER_EVERYTHING
    raw = path.read_text(encoding="utf-8", errors="replace")
    assert A_COMMENT_IN_THAT_FILE in raw, (
        "the comment this check is anchored to has been reworded; re-anchor it "
        "rather than deleting this test, which is the only thing keeping the "
        "reader honest")
    assert A_COMMENT_IN_THAT_FILE not in code_only(raw), (
        "code_only left a comment in place, so a name in prose would be read "
        "as a call")


def test_every_call_site_is_in_the_inventory() -> None:
    found = call_sites()
    new = sorted(set(found) - set(INVENTORY))
    assert not new, (
        "these calls to StatForSkill are not in INVENTORY in this file:\n"
        + "\n".join(f"  {file}\n    {args}" for file, args in new)
        + "\n\nEither a call site was added, or an existing one's arguments "
        "changed -- the key is the whole argument list, so a changed argument "
        "reads as a new entry and a gone one. Add it with ONE SHORT REASON for "
        "what it hands over, the way every entry there carries one. Issue #1992."
        f"\n\nScope: parsed by matching parentheses over comment-stripped and "
        f"string-stripped source, under {SOURCE_ROOT.name}, tests excluded. "
        f"{CALL_SITES} call sites were measured on {MEASURED_AT[:8]}.")


def test_the_inventory_holds_no_call_site_that_has_gone() -> None:
    """A list that keeps dead entries rots into an allowance that excuses everything."""
    found = call_sites()
    gone = sorted(set(INVENTORY) - set(found))
    assert not gone, (
        "INVENTORY lists these calls and the source no longer holds them:\n"
        + "\n".join(f"  {file}\n    {args}" for file, args in gone)
        + "\n\nA call site was removed or its arguments changed. Take the "
        "entry out, or update it to what the call now hands over.")


def test_a_call_that_hands_over_a_target_hands_over_the_distance_and_the_stagger() -> None:
    """Issue #1992's own finding, asserted rather than only inventoried."""
    short = []
    for key, args in call_sites().items():
        if len(args) <= TARGET or args[TARGET] == DEFAULTS[TARGET]:
            continue          # hands over no target, so this says nothing
        if args[DISTANCE] == DEFAULTS[DISTANCE] or args[STAGGER] == DEFAULTS[STAGGER]:
            if key not in TARGET_WITHOUT_THE_DISTANCE_OR_THE_STAGGER:
                short.append(key)
    assert not short, (
        "these calls hand over a target and leave the distance or the stagger "
        "at its default:\n"
        + "\n".join(f"  {file}\n    {args}" for file, args in short)
        + "\n\nA row on that stat asking target_within_metres or "
        "target_is_staggered would be accepted by every check, ship, and grant "
        "nothing. Pass them, or list the call in "
        "TARGET_WITHOUT_THE_DISTANCE_OR_THE_STAGGER with the reason. "
        "Issue #1992.")


def test_the_exemption_list_holds_nothing_that_now_hands_them_over() -> None:
    """A name that gained its arguments must leave the list, or the list rots."""
    found = call_sites()
    fixed = []
    for key in TARGET_WITHOUT_THE_DISTANCE_OR_THE_STAGGER:
        args = found.get(key)
        if args is None:
            continue          # the other test reports a vanished call site
        if (args[DISTANCE] != DEFAULTS[DISTANCE]
                and args[STAGGER] != DEFAULTS[STAGGER]):
            fixed.append(key)
    assert not fixed, (
        f"{sorted(fixed)} are listed as deliberately not handing over the "
        "distance and the stagger, and now hand over both. Take them out of "
        "TARGET_WITHOUT_THE_DISTANCE_OR_THE_STAGGER.")
