// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmSkillShape.h"
#include "CataclysmSkillTemplate.generated.h"

class ACataclysmGroundZone;

/**
 * What every shape template shares: its designed identity and its numbers.
 *
 * ONE CLASS PER SHAPE, NOT ONE PER SKILL. Issue #37 asks for this directly:
 * "Build these 18 abilities from shared templates, not as one-off
 * implementations. The full matrix is 558 rows. If the first 18 each need
 * bespoke work, the remaining 540 are unaffordable." The matrix is 398 rows
 * after issue #23 cut it, and the point stands.
 *
 * A SKILL IS A ROW, NOT A CLASS. The name, the description, the shape and the
 * shape's numbers all arrive from game/Data/WeaponSkills.csv when the weapon is
 * equipped, stamped onto the granted instance by
 * UCataclysmWeaponSlotsComponent. So adding a skill of an existing shape is a
 * workbook edit and needs no C++ at all, which is the acceptance criterion the
 * issue names second.
 */
UCLASS(Abstract)
class CATACLYSM_API UCataclysmSkillTemplate : public UCataclysmGameplayAbility
{
	GENERATED_BODY()

public:
	UCataclysmSkillTemplate();

	/** The designed skill's name, from the weapon skill matrix. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FString SkillName;

	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FString SkillDescription;

	/** This skill's numbers, parsed from its Shape Params cell. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Skill")
	FCataclysmSkillShapeParams Params;

	/** Which shape this class implements. Every subclass answers. */
	virtual ECataclysmSkillShape Shape() const PURE_VIRTUAL(UCataclysmSkillTemplate::Shape, return ECataclysmSkillShape::None;);

	/** Percent of weapon damage one use deals, from this ability's slot. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Skill")
	float GetSlotDamagePercent() const;

protected:
	/**
	 * Spend the cost, start the cooldown, and say whether the skill may run.
	 *
	 * EVERY TEMPLATE CALLS THIS FIRST AND NOTHING CALLED IT BEFORE. Issue #155
	 * built CheckCost, ApplyCost, CheckCooldown and ApplyCooldown on
	 * UCataclysmGameplayAbility, and the engine only invokes the two Apply
	 * halves from CommitAbility -- which no ability in the project called. So a
	 * skill's mana was checked and never spent and its cooldown was checked and
	 * never started, and every ability could be used continuously. The only
	 * ability that existed was the placeholder that does nothing, so nothing
	 * showed it. Cataclysm.Skills.UsingASkillSpendsManaAndStartsItsCooldown now
	 * fails if this is removed.
	 */
	bool CommitAndBegin(const FGameplayAbilitySpecHandle Handle,
						const FGameplayAbilityActorInfo* ActorInfo,
						const FGameplayAbilityActivationInfo ActivationInfo);

	/** The actor doing the hitting: the avatar, not the player state. */
	AActor* Avatar() const;

	/** Where the player is pointing, or the avatar's location if unknown. */
	FVector AimPoint() const;

	/**
	 * The direction the skill is aimed in, on the ground plane.
	 *
	 * FALLS BACK TO THE CASTER'S FACING RATHER THAN TO NOTHING, and that matters
	 * for more than tidiness. Anything with no cursor -- an enemy, a summoned
	 * minion, an automation test -- aims at its own feet through AimPoint, and a
	 * projectile aimed at its own feet has a flight path of zero length and hits
	 * nobody. It fires forward instead.
	 */
	FVector AimDirection() const;

	/**
	 * A point RangeCm away in the aimed direction, or nearer if the aim is nearer.
	 *
	 * Where a projectile lands, where a blink arrives, where a rift is torn.
	 */
	FVector AimedPointWithin(float RangeCm) const;

	/**
	 * Deal one hit to each target and set them alight if the skill says so.
	 *
	 * @param DamagePercent  negative takes the slot's figure
	 * @return how much damage was sent, summed, before mitigation
	 */
	float HitTargets(const TArray<AActor*>& Targets, float DamagePercent = -1.0f);

	/**
	 * Leave a burning patch of ground, if this skill's numbers say to.
	 *
	 * Eight of the sixteen designed Demonic skills do, on top of whatever else
	 * they are, which is why this is here and not a shape of its own.
	 */
	ACataclysmGroundZone* LeaveGroundAt(const FVector& Location);

	/** Pay HealthCostPercent of current health. Only Blood Pyre has one. */
	void PayHealthCost();
};
