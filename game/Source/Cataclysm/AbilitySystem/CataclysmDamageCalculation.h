// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CataclysmDamageCalculation.generated.h"

class UAbilitySystemComponent;

/**
 * What an incoming hit is, as far as the defender's mitigation cares.
 *
 * Kept separate from the gameplay effect that carries the damage so the
 * calculation can be tested directly, without building an effect spec.
 */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmIncomingHit
{
	GENERATED_BODY()

	/** Damage before any mitigation. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	float Damage = 0.0f;

	/** Which of the eight resistances applies. Empty means none of them do. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	FName DamageType;

	/**
	 * Which of the eight this hit is DRAWN as. Empty means the effect keeps the
	 * colours it was authored with.
	 *
	 * A SECOND FIELD BECAUSE THESE ARE TWO QUESTIONS. `DamageType` above answers
	 * "which resistance applies", and for a player's hit the answer is none: an
	 * enemy holds one generic resistance and has nothing to choose between, so a
	 * player's damage arrives untyped by design. That was settled on 2026-08-12
	 * and it is still true.
	 *
	 * But a player's skill DOES have a damage type -- 51 of the 58 shaped rows
	 * of `game/Data/WeaponSkills.csv` are Demonic and 7 are War -- and it is
	 * what the bolt and the burst should be coloured by. Answering both
	 * questions with one field meant every player effect drew white, which is
	 * the colour that was chosen to mean "nothing set this". Issue #803.
	 *
	 * FOR AN ENEMY THE TWO ARE ALWAYS EQUAL, because an enemy's damage is typed
	 * and the type is the same tag. The fields only differ for a player.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	FName EffectDamageType;

	/** Percentage points subtracted from the defender's resistance. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	float ResistancePenetration = 0.0f;

	/** Percentage of the defender's armor ignored, from gear and sub-type. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	float ArmorPenetration = 0.0f;

	/**
	 * The attacker's chance to critically strike, 0-100.
	 *
	 * READ OFF THE ATTACKER AND CARRIED, for the same reason the two penetration
	 * figures above are: `Resolve` is given the defender and never sees who is
	 * swinging. Zero means this hit cannot critically strike, which is the case
	 * for a damage over time tick and for a minion's blow.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	float CritChance = 0.0f;

	/**
	 * What a critical strike is worth, as a percentage. 150 means one and a half
	 * times the hit. The design's default class stat line supplies 150 and no
	 * class overrides it.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	float CritMultiplier = 150.0f;

	/** Area damage cannot be evaded. It can still be blocked. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsArea = false;

	/**
	 * Bleed, poison, burn and the rest. Routed differently: an energy shield
	 * does not absorb it, though it does still restart the shield's recharge.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsDamageOverTime = false;

	/** 10% more damage to what reaches health. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsSlashing = false;

	/** Strips 10% more energy shield per hit. */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsMagic = false;

	/**
	 * Ignores a further share of the defender's armor, on top of ArmorPenetration.
	 *
	 * A FLAG RATHER THAN A NUMBER ADDED BY THE CALLER, so the combination lives
	 * in `Resolve` where the rest of the mitigation order lives. That mirrors
	 * `Attacker.total_armor_ignored` in `sim/cataclysm_sim/damage.py`, which adds
	 * `PIERCING_ARMOR_IGNORED` to whatever the attacker's own stat holds and
	 * clamps the sum. A caller adding the 20% itself would put the same rule in
	 * as many places as there are callers.
	 *
	 * Issue #639.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsPiercing = false;

	/**
	 * Carries a chance to stun rather than a damage bonus.
	 *
	 * THE ONLY SUB-TYPE WHOSE EFFECT IS NOT DAMAGE, which is why it is read by
	 * the attribute set that resolves a hit rather than by `Resolve`: a stun is
	 * applied through `UCataclysmSkillEffects::ApplyStun`, which enforces the
	 * three anti-stun-lock rules, and `Resolve` is pure arithmetic with no way
	 * to reach any of that. Issue #639.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsBlunt = false;

	/**
	 * Whether the blow was struck in melee. Issue #1032.
	 *
	 * NOT A WEAPON SUB-TYPE LIKE THE FOUR ABOVE, and that is why it sits apart
	 * from them. Slashing, piercing, magic and blunt are properties of the
	 * WEAPON in the attacker's hand; this is a property of how the blow was
	 * thrown, and a ranged weapon and a melee one can both be slashing.
	 *
	 * READ FROM `Type.Melee` ON THE EFFECT, the same way `bIsArea` and
	 * `bIsDamageOverTime` are read from their own tags. Both sides already carry
	 * it: six of the seven enemy abilities since issue #1020, and 27 rows of
	 * `game/Data/WeaponSkills.csv` including every Fist skill the Masochist uses.
	 *
	 * `Resolve` DOES NOT READ IT, and no arithmetic depends on it. Melee is not
	 * a mitigation layer; what asks about it is a rule that fires when a blow
	 * lands, which is `UCataclysmVitalAttributeSet`'s job rather than this one's.
	 * Mutilation Mastery is the first: "Your melee critical strikes have a 5%
	 * chance per point to apply Bleeding."
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Cataclysm|Damage")
	bool bIsMelee = false;
};

/** What the calculation decided, step by step, so it can be inspected. */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmDamageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	bool bEvaded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	bool bBlocked = false;

	/**
	 * The hit landed as a critical strike and its damage above already carries
	 * the multiplier.
	 *
	 * ON THE RESULT AND NOT ONLY ON THE HIT, because the two functions that pick
	 * a floating damage number's colour and text are given the result alone. A
	 * flag on the incoming hit would be invisible to everything that draws.
	 *
	 * NEVER TRUE ON AN EVADED HIT. The roll is made after the evasion step, so a
	 * hit that never landed is not reported as a critical strike that never
	 * landed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	bool bWasCritical = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	float AbsorbedByMana = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	float AbsorbedByShield = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Damage")
	float DealtToHealth = 0.0f;
};

/**
 * The damage calculation, ported from `sim/cataclysm_sim/damage.py`.
 *
 * THE ORDER, from the Damage Calculation section of the design document:
 * evasion, block, armor, resistance, flat damage reduction, mana, energy
 * shield, health.
 *
 * WHY THIS IS A SEPARATE CLASS rather than sitting inside the attribute set:
 * every step of it is arithmetic on numbers, and pulling it out means the whole
 * order can be tested by passing values in, without constructing an ability
 * system component and an effect spec for each case.
 */
UCLASS()
class CATACLYSM_API UCataclysmDamageCalculation : public UObject
{
	GENERATED_BODY()

public:
	/** K in `armor / (armor + K)` is this times the difficulty tier. */
	static constexpr float ArmorConstantPerTier = 800.0f;

	/** Armor alone never removes more than this share of a hit. */
	static constexpr float ArmorReductionCap = 75.0f;

	/** How much resistance is worth against damage, however high it is. */
	static constexpr float ResistanceCap = 70.0f;

	/**
	 * The most the flat damage reduction stat removes from a hit.
	 *
	 * IT WAS THE ONE LAYER WITH NOTHING HOLDING IT. Evasion has a soft cap and
	 * covers direct attacks only, block removes half a hit so even certainty is
	 * not immunity, armour follows a curve that cannot reach 100 and is capped at
	 * 75 besides, and resistance is capped at 70. This is a flat percentage off
	 * everything, with no curve, no roll and no per-type split, and at 100 it was
	 * exact immunity. The design document says "No combination of these layers
	 * reaches immunity. Each has either a cap or a curve that cannot reach zero
	 * damage", and the second sentence was not true of this one. Issue #644.
	 *
	 * THE SAME FIGURE AS ArmorReductionCap, so the design has one number for the
	 * most a single unconditional mitigation layer may remove. Path of Exile caps
	 * the closest thing it ships at 90%, and that was deliberately not copied:
	 * its 90% covers physical damage alone where this covers all eight types.
	 *
	 * Mirrors `DAMAGE_REDUCTION_CAP` in `sim/cataclysm_sim/damage.py`, and
	 * `tools/tests/test_the_damage_reduction_cap_is_one_number.py` fails if the
	 * two ever disagree.
	 */
	static constexpr float DamageReductionCap = 75.0f;

	/**
	 * The most one "more" damage reduction source may remove, as a percentage.
	 *
	 * NOT THE SAME BOUND AS `DamageReductionCap` ABOVE, and it is not a balance
	 * figure. That one bounds the additive pool. The project owner decided on
	 * 2026-08-17 that it binds that pool only, because sources in the
	 * multiplicative bucket each remove a share of what the ones before them
	 * left and so cannot reach 100% however many there are.
	 *
	 * THIS BOUNDS ONE SOURCE, and exists only to keep that sentence true: a
	 * single factor of exactly 100 would remove all the damage and make a
	 * character immune, which is the failure `DamageReductionCap` was added for.
	 * Nothing in the design comes near it -- the largest multiplicative node in
	 * any class tree is 3% per point over 8 points, which is 24%.
	 *
	 * Mirrors `MORE_DAMAGE_REDUCTION_CAP` in `sim/cataclysm_sim/damage.py`.
	 * Issue #665.
	 */
	static constexpr float MoreDamageReductionCap = 99.0f;

	/**
	 * The two stats saying what share of a hit this character actually takes.
	 *
	 * ONE FOR EVERY HIT AND ONE FOR A DAMAGE OVER TIME TICK, and a tick meets
	 * BOTH. Issue #1026. Two stats rather than one scoped modifier, because
	 * `RequiredTags` scopes a modifier to the skill in the character's own hand
	 * and a character being hit is not swinging -- `DefenderStat` in the
	 * implementation file says so at length. The hit's own nature picks which
	 * stats are read, the way `ResistanceFor` beside it picks a resistance slot
	 * from the hit's damage type.
	 *
	 * NAMED CONSTANTS RATHER THAN LITERALS, because
	 * `UCataclysmPlayerClassStats` names the same two strings in two maps and a
	 * spelling that stopped matching would silently leave the stat with no base
	 * and no attribute. That is the failure issue #1025 was about.
	 */
	static const TCHAR* DamageTakenStat;
	static const TCHAR* DamageOverTimeTakenStat;

	/**
	 * What either of those two reads when nothing has changed it.
	 *
	 * A HUNDRED IS THE IDENTITY FOR A MULTIPLIER, so a character with no node
	 * spent takes exactly the damage the layers above it left. Every node that
	 * moves these is written as a percentage of what would otherwise arrive, and
	 * a base of zero would leave an increase and a `more` both worth nothing.
	 *
	 * IT IS BOTH THE ATTRIBUTE'S STARTING VALUE AND THE STAT'S BASE.
	 * `UCataclysmCombatAttributeSet` starts both attributes here, which is what
	 * an enemy holds, and `UCataclysmPlayerClassStats::EngineSuppliedBases`
	 * states the same figure as the base a player's stat line is built from.
	 */
	static constexpr float NormalDamageTaken = 100.0f;

	/** Negative resistance means taking extra damage. This bounds how bad. */
	static constexpr float ResistanceFloor = -100.0f;

	/** A successful block removes this share of the hit. */
	static constexpr float BlockDamageReduction = 50.0f;

	/** Slashing against health, and magic against energy shield. */
	static constexpr float SubtypeBonus = 10.0f;

	/**
	 * A piercing weapon ignores this share of the defender's armor, on top of
	 * whatever the attacker's own armour penetration stat holds.
	 *
	 * Mirrors `PIERCING_ARMOR_IGNORED` in `sim/cataclysm_sim/damage.py`, and the
	 * Weapon Sub-Types table it comes from. Issue #639.
	 */
	static constexpr float PiercingArmorIgnored = 20.0f;

	/**
	 * A Blunt weapon's chance to stun on every hit, as a percentage.
	 *
	 * Mirrors `BLUNT_STUN_CHANCE` in `sim/cataclysm_sim/damage.py`. It is part of
	 * the same pool an affix adds to, settled by the project owner on
	 * 2026-08-16, rather than a mechanic of its own.
	 */
	static constexpr float BluntStunChance = 10.0f;

	/**
	 * How long a stun applied by a chance lasts before the overflow lengthens
	 * it, in seconds.
	 *
	 * Mirrors `INCIDENTAL_STUN_SECONDS`. Deliberately the shortest duration any
	 * designed skill uses, so something that can stun on every hit does not
	 * outclass the skills whose whole purpose is stunning.
	 */
	static constexpr float IncidentalStunSeconds = 0.75f;

	/** Chance to stun caps here, and everything past it becomes duration. */
	static constexpr float StunChanceCap = 100.0f;

	/**
	 * The longest a stun may last however much chance is stacked, in seconds.
	 *
	 * Mirrors `LONGEST_STUN_SECONDS`. It is the longest stun the design already
	 * contains -- the Brute's Heart ten-piece set bonus -- and it is below the 5
	 * second immunity window on purpose: a stun as long as the window would hold
	 * a target for ever, which is the failure the whole anti-stun-lock rule
	 * exists to stop.
	 */
	static constexpr float LongestStunSeconds = 3.0f;

	/**
	 * Split a total chance to stun into the chance it applies at and how long
	 * the stun then lasts.
	 *
	 * THE RULE THE PROJECT OWNER SET FOR DAMAGE OVER TIME ON 2026-08-03, applied
	 * to stun on 2026-08-16: chance caps at 100% and everything past it
	 * multiplies the magnitude. A stun has no damage, so its magnitude is its
	 * duration. Mirrors `stun_application` in `sim/cataclysm_sim/damage.py`.
	 *
	 * @param TotalChance   every source added together, uncapped
	 * @param OutChance     the chance the stun is rolled at, capped at 100
	 * @param OutSeconds    how long it lasts, capped at LongestStunSeconds
	 */
	static void StunApplication(float TotalChance, float& OutChance,
								float& OutSeconds);

	/** Seconds after taking damage before an energy shield starts refilling. */
	static constexpr float EnergyShieldRechargeDelay = 3.0f;

	/** What every damage type's gameplay tag begins with. `Element.` */
	static const TCHAR* ElementTagPrefix;

	/**
	 * The tag that says a hit swept a volume rather than touching one target.
	 *
	 * `Type.AOE`, the parent of the three the vocabulary declares -- Aura,
	 * Persistent and PointBlank. A parent matches any of its children, so an
	 * effect carrying a specific one is area damage without having to list them.
	 */
	static const TCHAR* AreaDamageTagName;

	/**
	 * The tag that says a hit is damage over time.
	 *
	 * `Keyword.DoT`, the parent of Bleed, Burn, Disease, Generic, Necrosis,
	 * Poison and VoidSplinter.
	 */
	static const TCHAR* DamageOverTimeTagName;

	/**
	 * The tag that says a blow was struck in melee. Issue #1032.
	 *
	 * `Type.Melee`. Both sides already carry it: six of the seven enemy
	 * abilities since issue #1020, and 27 rows of `game/Data/WeaponSkills.csv`
	 * including every Fist skill the Masochist uses.
	 *
	 * NOT A PARENT OF ANYTHING, unlike the two above. It is a leaf, so a hit
	 * either carries it or does not.
	 */
	static const TCHAR* MeleeTagName;

	/**
	 * The tag that says a hit may not critically strike, whoever threw it.
	 *
	 * `Keyword.NoCrit`. IT EXISTS FOR SUMMONED MINIONS. A minion's blow is dealt
	 * in its summoner's name -- `ACataclysmMinion` calls
	 * `UCataclysmSkillEffects::ApplyHit` with the summoner as the attacker --
	 * so the attacker the engine sees when it reads a critical strike chance is
	 * the player. The design forbids that inheritance in as many words:
	 * "A minion takes neither the summoner's critical strike chance nor its
	 * multiplier", and minion damage was set at the top of its band precisely
	 * because it has no critical strike layer to compound with.
	 * See `docs/Cataclysm_GDD_v2.md` lines 1747 and 1776.
	 *
	 * A TAG RATHER THAN A FIELD, because this has to survive the trip from the
	 * caller to the defender's attribute set, and a gameplay effect spec carries
	 * tags. It is the same route `Type.AOE` and `Keyword.DoT` already take.
	 */
	static const TCHAR* NoCriticalStrikeTagName;

	/**
	 * The tag that says a hit ignores none of the target's armour or resistance.
	 *
	 * `Keyword.NoPenetration`. IT EXISTS FOR SUMMONED MINIONS, and for the same
	 * reason `Keyword.NoCrit` above does: a minion's blow is dealt in its
	 * summoner's name, so the attacker the engine reads a penetration figure off
	 * is the player. The design blocks that inheritance twice over. It names
	 * penetration in the list of what does not cross -- "A minion does not take
	 * the summoner's weapon damage, flat added damage, attack speed, critical
	 * strike chance or multiplier, penetration..." -- and it states the general
	 * rule the list is an instance of: "A minion reaches its summoner through
	 * exactly three channels, and nothing else crosses", those three being its
	 * side, its base health and damage raised by the summoner's level, and
	 * increased damage from one primary attribute. See
	 * `docs/Cataclysm_GDD_v2.md` line 1747 and the table above it.
	 *
	 * IT STOPS BOTH PENETRATION STATS AND THE WEAPON SUB-TYPE'S SHARE. There are
	 * three ways a summoner's armour penetration reaches a minion's blow, not
	 * one: the `Penetration` attribute, the `ArmorPenetration` attribute, and a
	 * piercing weapon, which `Resolve` turns into a further 20% of armour
	 * ignored. The third arrives by a different route -- the weapon sub-type is
	 * read off the effect causer rather than off the attacker's attributes -- and
	 * a fix that stopped only the first two would leave a minion penetrating
	 * armour whenever its summoner held a piercing weapon.
	 *
	 * A MINION HAS NO PENETRATION OF ITS OWN TO PUT IN ITS PLACE, so this leaves
	 * a minion's blow at zero. `game/Data/MinionTypes.csv` states no penetration
	 * for any type, `game/Data/MinionScaling.csv` scales only damage, and
	 * `ACataclysmMinion` carries no combat attribute set to hold one. If a
	 * minion-scoped penetration modifier is ever written, it belongs on the
	 * minion rather than here: the design's rule is that a modifier reaches a
	 * minion only when it names minions.
	 */
	static const TCHAR* NoPenetrationTagName;

	/**
	 * The tag that says a hit carries none of the attacker's weapon sub-type.
	 *
	 * `Keyword.NoWeaponSubType`. IT EXISTS FOR SUMMONED MINIONS, the third of
	 * three exclusions built for the same reason. A minion's blow is dealt in its
	 * summoner's name, and the weapon a hit is credited to is read off the effect
	 * causer, which is that same summoner, so without this a minion inherits
	 * whatever is in the player's hand: a sword makes it deal 10% more to health
	 * and a wand makes it strip 10% more energy shield.
	 *
	 * THE DESIGN'S GENERAL RULE IS WHAT BLOCKS IT, rather than a sentence naming
	 * sub-types. "A minion reaches its summoner through exactly three channels,
	 * and nothing else crosses" -- its side, its base health and damage raised by
	 * the summoner's level, and increased damage from one primary attribute --
	 * followed by "Everything else is blocked unless a modifier says minion."
	 * A weapon sub-type is not one of the three. See
	 * `docs/Cataclysm_GDD_v2.md:1747` and the table above it. Issue #676.
	 *
	 * IT OVERLAPS WITH `Keyword.NoPenetration` ON PIERCING, on purpose. Piercing's
	 * whole effect is armour penetration, so a hit that may not penetrate must not
	 * get it either. Two independent reasons block the same flag and a hit needs
	 * only one of them.
	 */
	static const TCHAR* NoWeaponSubTypeTagName;

	/** `Keyword.NoLeech`. See NoLeechTag. */
	static const TCHAR* NoLeechTagName;

	/** `Keyword.NoRetaliation`. See NoRetaliationTag. */
	static const TCHAR* NoRetaliationTagName;

	/**
	 * The key a hit's own critical strike chance is carried under.
	 *
	 * `Data.SkillCritChance`. NOT A TAG ANYTHING IS SCOPED BY. The other tag
	 * names on this class go into a container and are asked yes-or-no questions.
	 * This one names a NUMBER: Unreal's set-by-caller magnitudes are a map from
	 * tag to float on a gameplay effect spec, and this is that map's key. It
	 * never appears in a tag container, so nothing can match on it by accident.
	 *
	 * `Data.` IS THE ENGINE'S OWN NAMESPACE FOR THIS. Unreal's samples key
	 * set-by-caller magnitudes `Data.Damage`, `Data.Duration` and so on, and this
	 * follows that rather than inventing a scheme.
	 *
	 * WHY A NUMBER HAS TO TRAVEL AT ALL. Critical strike chance belongs to the
	 * skill being used, not to the character using it, and a character holds six
	 * skills at once against one `CritChance` attribute. So the skill's own base
	 * rides on the hit and the attribute holds the default. Issue #657.
	 */
	static const TCHAR* SkillCritChanceDataTagName;

	/**
	 * The marker that says an effect's `Element.*` tag is about colour only.
	 *
	 * `Data.ElementIsForColourOnly`. A player's damage effect carries its
	 * skill's element tag so the bolt and the burst can be coloured by it, and
	 * carries this beside it so the defender does not read that tag as a
	 * resistance to apply. Issue #803.
	 *
	 * PUT ON AT THE SOURCE, NOT DECIDED AT THE FAR END. The obvious alternative
	 * was for the defender to ask who hit it and work the answer out. That
	 * breaks when the attacker is gone: an enemy's burn ticking on the player
	 * after the enemy is dead would find no attacker and stop applying the
	 * player's resistance to it part way through. The marker is stamped on the
	 * effect while the attacker is still there, so it survives.
	 *
	 * `Data.` IS THE ENGINE'S OWN NAMESPACE FOR A TAG THAT NAMES A MECHANISM
	 * rather than a property, which is why `Data.SkillCritChance` above uses it
	 * too. Nothing scopes a modifier by it.
	 */
	static const TCHAR* ElementIsForColourOnlyTagName;

	/** `Type.AOE`, or an invalid tag if the vocabulary has lost it. */
	static FGameplayTag AreaDamageTag();

	/** `Keyword.DoT`, or an invalid tag if the vocabulary has lost it. */
	static FGameplayTag DamageOverTimeTag();

	/** `Type.Melee`, or an invalid tag if the vocabulary has lost it. #1032. */
	static FGameplayTag MeleeTag();

	/** `Keyword.NoCrit`, or an invalid tag if the vocabulary has lost it. */
	static FGameplayTag NoCriticalStrikeTag();

	/** `Keyword.NoPenetration`, or an invalid tag if the vocabulary lost it. */
	static FGameplayTag NoPenetrationTag();

	/** `Keyword.NoWeaponSubType`, or an invalid tag if the vocabulary lost it. */
	static FGameplayTag NoWeaponSubTypeTag();

	/**
	 * A blow that gives its attacker no leech, whatever the attacker has.
	 *
	 * THE FOURTH EXCLUSION A SUMMONED MINION CARRIES, alongside no critical
	 * strike, no penetration and no weapon sub-type, and blocked by the same
	 * design sentence: a minion reaches its summoner through exactly three
	 * channels, and leech is named among what does not cross. Without it a
	 * minion's blow would leech for its summoner, because the blow is dealt
	 * in the summoner's name and the leech is read off the attacker.
	 */
	static FGameplayTag NoLeechTag();

	/**
	 * A blow that provokes no retaliation from what it strikes.
	 *
	 * THE FIFTH EXCLUSION A SUMMONED MINION CARRIES, and the only one of the
	 * five that protects the summoner rather than the target. Retaliation is
	 * dealt back to whoever the hit was credited to, and a minion's blow is
	 * credited to its summoner, so without this a caster standing well away
	 * from the fight would take damage every time one of its imps struck a
	 * retaliating enemy.
	 */
	static FGameplayTag NoRetaliationTag();

	/** `Data.SkillCritChance`, or an invalid tag if the vocabulary lost it. */
	static FGameplayTag SkillCritChanceDataTag();

	/**
	 * `Data.ElementIsForColourOnly`, or an invalid tag if the vocabulary lost
	 * it.
	 */
	static FGameplayTag ElementIsForColourOnlyTag();

	/**
	 * A damage type as the gameplay tag that carries it on an effect.
	 *
	 * "Demonic" becomes `Element.Demonic`. Returns an invalid tag for a name the
	 * tag vocabulary has no `Element.*` entry for, and for `NAME_None`.
	 *
	 * WHY A TAG RATHER THAN THE NAME ITSELF: a gameplay effect can carry tags and
	 * cannot carry an FName, and the eight `Element.*` tags already exist in
	 * `game/Config/Tags/CataclysmTags.ini` because stat modifiers scope to them.
	 * See `DamageTypeFromTags` for the way back.
	 */
	static FGameplayTag ElementTagFor(FName DamageType);

	/**
	 * The damage type an effect's tags say it is, or `NAME_None` for an untyped
	 * hit.
	 *
	 * THE INVERSE OF `ElementTagFor`, AND IT LIVES BESIDE IT ON PURPOSE. The two
	 * are the only encoding and decoding of a damage type in the project, and if
	 * they ever disagreed the type would vanish silently and every resistance
	 * would go back to doing nothing, which is exactly the state issue #486
	 * describes.
	 */
	static FName DamageTypeFromTags(const FGameplayTagContainer& Tags);

	/** Armor as a percentage of damage removed. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Damage")
	static float ArmorReduction(float Armor, int32 Tier);

	/**
	 * Resistance after penetration, then capped.
	 *
	 * Penetration is subtracted BEFORE the cap. That order is the only thing
	 * that makes over-capping worth anything: against 30 penetration a defender
	 * at 100 resistance still sits at the 70 cap, where one at exactly 70 drops
	 * to 40. Capping first would make every point above 70 worthless.
	 *
	 * PENETRATION STOPS AT ZERO. `docs/Cataclysm_GDD_v2.md` states in section X
	 * that "penetration beyond an enemy's resistance grants no bonus, so
	 * over-stacking it does not become a damage multiplier against the enemies
	 * that need it least". Until issue #482 the subtraction ran on into negative
	 * resistance, so 50 penetration against a 35% target landed 115% of a hit.
	 *
	 * A NATIVELY NEGATIVE RESISTANCE IS LEFT ALONE, which is why the floor is not
	 * simply zero. Enchantments push a target under zero deliberately and that
	 * target takes extra damage; penetration must not create that state itself.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Damage")
	static float EffectiveResistance(float Resistance, float Penetration);

	/**
	 * Flat damage reduction as a percentage of damage removed, capped.
	 *
	 * A FUNCTION RATHER THAN A CLAMP AT THE CALL SITE, so the cap lives in one
	 * place and the mitigation step stays one line, which is what ArmorReduction
	 * and EffectiveResistance already do for their own caps.
	 *
	 * THE FLOOR IS ZERO, WHICH IS WHAT SEPARATES IT FROM EffectiveResistance.
	 * That one floors at -100 on purpose, because a negative resistance means
	 * taking extra damage and several enchantments inflict one deliberately.
	 * Nothing in the design grants negative flat damage reduction.
	 *
	 * Mirrors `effective_damage_reduction` in `sim/cataclysm_sim/damage.py`.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Damage")
	static float EffectiveDamageReduction(float Reduction);

	/**
	 * Several "more" damage reductions as one percentage of damage removed.
	 *
	 * Each source removes a share of what the ones before it left, so two
	 * sources of 20% remove 36% between them and not 40%. That is the whole
	 * difference between this bucket and the additive pool
	 * `EffectiveDamageReduction` bounds, and it is the defensive half of the rule
	 * the design already states for offence: `(base + flat) x (1 + increases) x
	 * more1 x more2`, where each "more" is its own multiplier rather than joining
	 * a sum.
	 *
	 * TWELVE PASSIVE TREE NODES SAY "(multiplicative)" AND MEAN THIS. Eleven in
	 * the Bulwark tree and one in the Saboteur tree grant damage reduction and
	 * carry the word. The project owner confirmed on 2026-08-17 that
	 * multiplicative means "more", which is what Path of Exile and Last Epoch
	 * both call it. Issue #665.
	 *
	 * IT CANNOT REACH 100% AND THAT IS THE POINT. Every factor removes a share of
	 * what is left, so the product never reaches zero, and the 75% cap on the
	 * additive pool has nothing to prevent here. Each factor is separately
	 * clamped by `MoreDamageReductionCap`, because a single factor of exactly 100
	 * would be exact immunity.
	 *
	 * Mirrors `combined_more_damage_reduction` in `sim/cataclysm_sim/damage.py`.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Damage")
	static float CombinedMoreDamageReduction(const TArray<float>& Factors);

	/**
	 * Run one hit through the whole order against a character's attribute sets.
	 *
	 * `EvasionRoll`, `BlockRoll` and `CritRoll` are 0-100 values supplied by the
	 * caller so that tests can pin them. Pass a negative number to roll here.
	 */
	static FCataclysmDamageResult Resolve(const FCataclysmIncomingHit& Hit,
										  const UAbilitySystemComponent* Defender,
										  int32 Tier,
										  float EvasionRoll = -1.0f,
										  float BlockRoll = -1.0f,
										  float CritRoll = -1.0f);
};
