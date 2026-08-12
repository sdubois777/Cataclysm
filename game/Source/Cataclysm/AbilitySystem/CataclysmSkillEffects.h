// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CataclysmSkillEffects.generated.h"

class UAbilitySystemComponent;
class UDataTable;
class UGameplayEffect;
struct FGameplayEffectContextHandle;

/** How long an effect lasts and what it is worth. Read from the DoTs sheet. */
USTRUCT(BlueprintType)
struct CATACLYSM_API FCataclysmStatusEffectNumbers
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Status Effect")
	float DurationSeconds = 0.0f;

	/** The whole effect as a percent of the hit that applied it. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Status Effect")
	float PercentOfHit = 0.0f;

	/** False when no row was found, or when the row states no numbers. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Status Effect")
	bool bUsable = false;
};

/**
 * Dealing damage and applying effects, shared by every skill template.
 *
 * NOTHING COULD DO THIS BEFORE EITHER. UCataclysmDamageCalculation::Resolve was
 * called from exactly one place -- the defender's own attribute set, when the
 * Damage meta attribute changed -- and nothing in the project ever changed it.
 * Every number in the design that reads "250% weapon damage" was a percentage
 * of nothing, in the same way every cooldown reduction divided nothing before
 * issue #155.
 *
 * WHY EFFECTS ARE BUILT HERE RATHER THAN AUTHORED AS ASSETS. The magnitudes come
 * from generated data tables and from the caster's own attributes at the moment
 * of the hit, so there is no fixed number an authored asset could carry.
 * UCataclysmGameplayAbility::ApplyCooldown already builds its effect the same
 * way and for the same reason.
 */
UCLASS()
class CATACLYSM_API UCataclysmSkillEffects : public UObject
{
	GENERATED_BODY()

public:
	/** The name of the burn effect's row in the generated status effect table. */
	static const TCHAR* BurnRowName;

	/**
	 * What one basic attack from this character deals.
	 *
	 * The design's anchor: the Skill Slots sheet gives the basic attack 100%
	 * because it IS weapon damage, and every other slot is a percentage of it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static float WeaponDamageOf(const UAbilitySystemComponent* AbilitySystem);

	/**
	 * Deal one hit worth DamagePercent of the caster's weapon damage.
	 *
	 * THE CASTER'S OWN MODIFIERS APPLY HERE. Weapon damage times the skill's
	 * percentage is the BASE; the caster's stat modifiers then run over it
	 * through UCataclysmStatPipeline, which is where a self buff's magnitude
	 * becomes a number. Which modifiers reach it is decided by SkillTags: an
	 * increase scoped to Element.Demonic applies to a Demonic skill and to no
	 * other. Issue #166.
	 *
	 * @param SkillTags  the tags of the skill dealing the hit. An empty
	 *                   container means only unscoped and Scope.Global
	 *                   modifiers apply, which is the right answer for a hit
	 *                   that belongs to no skill.
	 * @return the damage sent, before the defender's mitigation. Zero when
	 *         either side is missing or the caster has no weapon damage.
	 */
	static float ApplyHit(AActor* Instigator, AActor* Target, float DamagePercent,
						  const FGameplayTagContainer& SkillTags = FGameplayTagContainer());

	/**
	 * What one hit of this size, from this caster, is worth after the caster's
	 * own stat modifiers.
	 *
	 * Separated from ApplyHit so a test can read the number without a defender,
	 * and so the burning ground can price a tick the same way a hit is priced.
	 */
	static float ModifiedDamage(const UAbilitySystemComponent* Source,
								float BaseDamage,
								const FGameplayTagContainer& SkillTags);

	/**
	 * Deal a hit of an amount already worked out.
	 *
	 * For the callers that hold an absolute number rather than a percentage:
	 * a burning patch of ground knows what one of its ticks is worth, because
	 * the skill that left it worked that out once when it was created.
	 *
	 * @return whether damage was sent
	 */
	static bool ApplyDirectDamage(AActor* Instigator, AActor* Target, float Damage);

	/**
	 * Set a target alight, for the duration and share the DoTs sheet states.
	 *
	 * ONE STACK ONLY, which the design states for every effect a player can
	 * apply. A second application refreshes the duration rather than adding a
	 * second burn, which matters here more than anywhere else: fifteen of the
	 * sixteen designed Demonic skills apply burn, and several apply it many
	 * times a second.
	 *
	 * @param HitDamage  the damage of the hit that caused it, before mitigation
	 * @return whether a burn was applied
	 */
	static bool ApplyBurn(AActor* Instigator, AActor* Target, float HitDamage);

	/** Burn's duration and damage share, or bUsable false if it has none. */
	static FCataclysmStatusEffectNumbers BurnNumbers();

	/**
	 * Which of the defender's eight resistances this attacker's damage is met by.
	 *
	 * An enemy's own damage type, and `NAME_None` for anything else. See the
	 * definition for why only one side of a fight is typed.
	 */
	static FName DamageTypeOf(const AActor* Attacker);

	/**
	 * Apply damage over a duration, in even ticks one second apart.
	 *
	 * @param TotalDamage  spread across the whole duration
	 * @param EffectTag    granted for the duration, and what makes it one stack
	 */
	static bool ApplyDamageOverTime(AActor* Instigator, AActor* Target,
									float TotalDamage, float DurationSeconds,
									const FGameplayTag& EffectTag);

	/**
	 * Grant a tag for a duration and nothing else.
	 *
	 * WHAT A BUFF OR A DEBUFF IS UNTIL ITS MAGNITUDE CAN BE APPLIED. Burning
	 * Wrath's increased fire damage, Martyr's Ember's stored damage and
	 * Subjugate's Madness all name effects this project has no attribute or hook
	 * for. Granting the tag makes the duration real and makes "is it up?" a
	 * question with a true answer, which is what everything else can be built
	 * against later. Issue #166.
	 *
	 * ONE STACK ONLY, refreshed rather than added to, as the design requires of
	 * every player-applied effect.
	 */
	static bool ApplyTagForDuration(AActor* Instigator, AActor* Target,
									const FGameplayTag& EffectTag,
									float DurationSeconds);

	/** Whether this actor currently carries the tag. */
	static bool HasTag(const AActor* Actor, const FGameplayTag& Tag);

	/** The Keyword.DoT.Burn tag, or an invalid tag if the vocabulary lacks it. */
	static FGameplayTag BurnTag();

	// --- Stun -----------------------------------------------------------------
	//
	// THE FIRST THING IN THE PROJECT THAT CAN HOLD A CHARACTER STILL. Section VI
	// of docs/Cataclysm_GDD_v2.md defines a stun as "the target cannot act at
	// all", separates it from a slow such as Cripple, and gives it three rules
	// against stun-locking. sim/cataclysm_sim/damage.py is the model those rules
	// are ported from and the names below follow it deliberately.
	//
	// The stun itself is a tag held for a duration, which is how every other
	// timed effect in this project works. What the tag stops is decided by the
	// things that read it: ACataclysmEnemyController refuses to act while it is
	// up and ACataclysmPlayerController refuses to move or activate a skill.

	/**
	 * How much of a target's maximum health one hit must take to stun it.
	 *
	 * The first anti-stun-lock rule, and the reason small hits cannot interrupt
	 * constantly. Measured against damage ACTUALLY DEALT rather than damage
	 * swung, so a hit that armour reduced to a scratch is a scratch.
	 * Mirrors STUN_DAMAGE_THRESHOLD in sim/cataclysm_sim/damage.py.
	 */
	static constexpr float StunDamageThresholdPercent = 10.0f;

	/**
	 * Seconds a target that has just been stunned cannot be stunned again for.
	 *
	 * The second anti-stun-lock rule. Mirrors STUN_IMMUNITY_SECONDS in
	 * sim/cataclysm_sim/damage.py, and it is the same five seconds the design
	 * reuses for displacement diminishing returns rather than a second number.
	 *
	 * NOTHING ENFORCED THIS BEFORE. damage.py says in terms that it resolves one
	 * hit with no clock and that the game enforces the window; the game had no
	 * stun at all, so this is the first implementation of it anywhere.
	 */
	static constexpr float StunImmunityWindowSeconds = 5.0f;

	/**
	 * Hold a target still for a duration, honouring the anti-stun-lock rules.
	 *
	 * THE THIRD RULE IS NOT CHECKED HERE, because it cannot be. "A boss cannot
	 * be stunned at all" needs a boss, and no boss concept exists anywhere in
	 * game/Source -- no flag, no class, no tag. Issue #395 covers adding one.
	 * Until it exists this function would have nothing to ask.
	 *
	 * @param DamageDealt       the damage this hit actually did, after the
	 *                          defender's mitigation. Ignored when the stun is
	 *                          designed.
	 * @param bStunIsDesigned   true for a stun that is the point of the attack,
	 *                          such as the Brute's Stomp. A designed stun skips
	 *                          the damage threshold, because an attack built to
	 *                          stun should not fail to when it lands. It does
	 *                          NOT skip the immunity window.
	 * @return whether a stun was applied
	 */
	static bool ApplyStun(AActor* Instigator, AActor* Target,
						  float DurationSeconds, float DamageDealt,
						  bool bStunIsDesigned);

	/** Whether this actor is stunned right now and may not act. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill Effects")
	static bool IsStunned(const AActor* Actor);

	/** The State.Stunned tag, or an invalid tag if the vocabulary lacks it. */
	static FGameplayTag StunnedTag();

	/** The State.StunImmune tag, or an invalid tag if the vocabulary lacks it. */
	static FGameplayTag StunImmuneTag();

private:
	/** Where the imported status effect table lives. */
	static const TCHAR* StatusEffectTableAssetPath;

	static const UDataTable* LoadStatusEffectTable();

	/**
	 * Apply a damage-carrying effect with the attacker's damage type on it.
	 *
	 * Every path that damages anything goes through here, so there is one place
	 * a hit's properties are attached and one place they can be forgotten.
	 */
	static void ApplyTypedSpec(UGameplayEffect* Effect,
							   const FGameplayEffectContextHandle& Context,
							   UAbilitySystemComponent* Defender,
							   const AActor* Attacker);
};
