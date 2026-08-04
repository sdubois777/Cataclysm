// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/CataclysmCharacterBase.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "CataclysmEnemyCharacter.generated.h"

class UCataclysmAbilitySystemComponent;
class UCataclysmVitalAttributeSet;
class UCataclysmCombatAttributeSet;
class UCataclysmResistanceAttributeSet;
class UStaticMeshComponent;

/**
 * Base for every enemy.
 *
 * Owns its ability system component directly, unlike the player. An enemy that
 * dies is destroyed and nothing about it needs to survive, so there is no reason
 * to separate the component from the pawn.
 */
UCLASS()
class CATACLYSM_API ACataclysmEnemyCharacter : public ACataclysmCharacterBase
{
	GENERATED_BODY()

public:
	ACataclysmEnemyCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void BeginPlay() override;

	/**
	 * Sets both maximum health and current health, before or after BeginPlay.
	 *
	 * WHY A SETTER RATHER THAN A PROPERTY ON THE CLASS. An enemy's real health
	 * comes from its rarity tier and the run's difficulty, neither of which
	 * exists yet, so nothing that ships should carry a hard-coded figure. This
	 * is for whoever is placing an enemy to say what they want, and today that
	 * is the sandbox training dummy spawner. Issue #39 replaces it.
	 *
	 * Both together, because setting the maximum alone leaves an enemy at its
	 * old current value, and setting the current alone is clamped to the old
	 * maximum -- so either one on its own quietly does nothing useful.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetHealth(float NewMaxHealth);

	/**
	 * Sets what one of its attacks is worth, before or after BeginPlay.
	 *
	 * Deferred the same way SetHealth is and for the same reason: writing to an
	 * attribute before the ability system has registered its attribute sets
	 * raises an engine ensure rather than failing quietly.
	 *
	 * WHY AN ENEMY HAS "WEAPON DAMAGE" AT ALL. It carries no weapon. The damage
	 * pipeline reads one number, the AttackDamage attribute, whether the attacker
	 * is a character holding a greataxe or a monster with claws, so this is what
	 * an enemy's claws are worth. Issue #39 replaces the setter with a figure
	 * derived from tier, floor and rarity.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Enemy")
	void SetAttackDamage(float NewAttackDamage);

	/** A stand-in body, so an enemy is visible before there is any art. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	//~ Driven by ACataclysmEnemyController
	virtual float AttackReachCm() const override { return MeleeReachCm; }
	virtual float SightRadiusCm() const override { return NoticeRadiusCm; }
	virtual float SecondsBetweenAttacks() const override { return AttackIntervalSeconds; }
	virtual void AttackTarget(AActor* Target) override;
	//~ End

	/**
	 * How close it must be to hit, in centimetres.
	 *
	 * A JUDGEMENT, NOT A DESIGN FIGURE, and so are the two below. Nothing in the
	 * design states any of them. Two metres is a little over twice the capsule
	 * radius, which is about the distance at which two of these placeholder
	 * cylinders look like they are touching.
	 *
	 * Per enemy rather than one constant for all of them, which is the shape
	 * Diablo II uses: its monstats.txt gives every monster type its own vision
	 * distance. Issue #39's seven enemies are the reason -- a Hellhound that
	 * charges and a Corrupted Sentinel that never moves cannot share one number.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.0"))
	float MeleeReachCm = 200.0f;

	/** How far it notices something to attack, in centimetres. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.0"))
	float NoticeRadiusCm = 1500.0f;

	/** Seconds between its attacks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cataclysm|Enemy", meta = (ClampMin = "0.01"))
	float AttackIntervalSeconds = 1.5f;

	/**
	 * What one attack deals, as a percent of its own attack damage.
	 *
	 * 100, because the Skill Slots sheet of the design workbook gives the basic
	 * attack 100% on the grounds that it IS weapon damage and every other slot
	 * is a percentage of it. An enemy has only this until issue #39 gives each
	 * one designed abilities.
	 */
	static constexpr float AttackPercentOfOwnDamage = 100.0f;

protected:
	virtual void InitAbilityActorInfo() override;

	/**
	 * Writes StartingMaxHealth and StartingAttackDamage onto the attributes, if
	 * they are ready for it.
	 *
	 * Called both from the setters and from InitAbilityActorInfo, so it does not
	 * matter which happens first.
	 */
	void ApplyStartingAttributes();

	/** What SetHealth was last asked for. Zero means the attribute set's own default. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float StartingMaxHealth = 0.0f;

	/** What SetAttackDamage was last asked for. Zero means it deals nothing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cataclysm|Enemy")
	float StartingAttackDamage = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

	/** Three sets, not the player's five. See the constructor for why. */
	UPROPERTY()
	TObjectPtr<UCataclysmVitalAttributeSet> VitalAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmCombatAttributeSet> CombatAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmResistanceAttributeSet> ResistanceAttributes;

private:
	FCataclysmAbilitySetHandles GrantedHandles;
};
