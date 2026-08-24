// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "Character/CataclysmClassStats.h"
#include "CataclysmPlayerState.generated.h"

class UCataclysmAbilitySystemComponent;
class UCataclysmVitalAttributeSet;
class UCataclysmPrimaryAttributeSet;
class UCataclysmCombatAttributeSet;
class UCataclysmResistanceAttributeSet;
class UCataclysmClassResourceAttributeSet;

/**
 * Owns the player's ability system component and attribute sets.
 *
 * WHY THE PLAYER STATE AND NOT THE PAWN.
 *
 * The design has the player die and respawn at the capital, at a cost of 5 to 15
 * days depending on difficulty. Death is a routine, repeated event, not the end
 * of a session. A pawn is destroyed on death; the player state is not.
 *
 * Putting the ability system on the pawn would mean every death destroyed the
 * player's attributes, active effects, cooldowns and granted abilities, and all
 * of it would have to be saved and restored by hand. On the player state, it
 * simply survives.
 *
 * Enemies do the opposite and own their component on the pawn, because an enemy
 * that dies is gone.
 */
UCLASS()
class CATACLYSM_API ACataclysmPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACataclysmPlayerState();

	//~ IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UCataclysmAbilitySystemComponent* GetCataclysmAbilitySystemComponent() const { return AbilitySystemComponent; }

	const UCataclysmVitalAttributeSet* GetVitalAttributes() const { return VitalAttributes; }
	const UCataclysmPrimaryAttributeSet* GetPrimaryAttributes() const { return PrimaryAttributes; }
	const UCataclysmCombatAttributeSet* GetCombatAttributes() const { return CombatAttributes; }
	const UCataclysmResistanceAttributeSet* GetResistanceAttributes() const { return ResistanceAttributes; }
	const UCataclysmClassResourceAttributeSet* GetClassResourceAttributes() const { return ClassResourceAttributes; }

	/**
	 * The attribute points this character has spent.
	 *
	 * HERE FOR THE SAME REASON THE ATTRIBUTE SETS ARE. A pawn is destroyed on
	 * death and the player state is not, and an allocation a player lost every
	 * time they died would be worse than no allocation at all.
	 *
	 * ONE PER LEVEL IS THE WHOLE SUPPLY TODAY. `docs/Cataclysm_GDD_v2.md` says
	 * "Players gain 1 attribute point per level" and names the Maw as a second
	 * source; the Maw does not exist, so AttributePointsAvailable below is the
	 * character's level and nothing else. Issue #50.
	 */
	const FCataclysmAttributePoints& GetSpentAttributePoints() const { return SpentAttributePoints; }

	/** How many a character may spend altogether. Its level, until the Maw exists. */
	int32 AttributePointsAvailable() const;

	/** How many of those are not spent yet. */
	int32 AttributePointsUnspent() const;

	/**
	 * Spend into one attribute, named as `game/Data/Attributes.csv` names it.
	 *
	 * REFUSED RATHER THAN CLAMPED when the character does not have that many,
	 * and `OutReason` says which refusal it was. Clamping would let "spend 40"
	 * quietly become "spend 3" and still read as success.
	 */
	bool SpendAttributePoints(const FString& Attribute, int32 Count, FString& OutReason);

	/** Return every spent point, so they can be spent again. */
	void ResetAttributePoints();

	// ----------------------------------------------------------------------
	// Level and experience
	// ----------------------------------------------------------------------

	/**
	 * The character's level, 1 to 100.
	 *
	 * WHY IT IS HERE AND NOT ON THE PAWN, which is the reason the ability system
	 * and the spent attribute points are here: a pawn is destroyed on death and
	 * the player state is not, and a level lost on every death would be worse
	 * than no levelling at all.
	 *
	 * FALLS BACK TO `Cataclysm.PlayerLevel` UNTIL A LEVEL HAS BEEN DECIDED,
	 * which is what `LevelNotYetDecided` below means. That console variable was
	 * the only level this project had, three call sites read it, and every
	 * automation test wanting a level 40 character sets it. Keeping it as the
	 * STARTING level means levelling arrives without moving any of that, and a
	 * character that has neither gained a level nor loaded a save behaves
	 * exactly as it did before.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Experience")
	int32 GetCharacterLevel() const;

	/**
	 * Experience earned toward the next level.
	 *
	 * PROGRESS INTO THE CURRENT LEVEL, NOT A RUNNING TOTAL, matching
	 * `FCataclysmCharacterRecord::Experience` and for the reason
	 * `UCataclysmExperience::Grant` gives: a running total would make the level
	 * derivable and therefore a second copy of the same fact, and retuning the
	 * curve would then silently move every existing character.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Experience")
	int64 GetExperienceIntoLevel() const { return ExperienceIntoLevel; }

	/**
	 * Add experience, raising the level as far as it pays for.
	 *
	 * @return how many levels were gained, which is what a caller awarding a
	 *         point per level needs and cannot recover afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Experience")
	int32 GrantExperience(int64 Amount);

	/**
	 * Put a saved level and progress back onto the character.
	 *
	 * CLAMPED RATHER THAN REFUSED, because this is reached from a save record
	 * and a save record holds whatever was last written to it. Refusing would
	 * leave the character at whatever level it happened to have, which is a
	 * worse answer than the nearest legal one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Experience")
	void SetLevelAndExperience(int32 NewLevel, int64 NewExperience);

protected:
	/**
	 * What `CharacterLevel` holds before anything has decided one.
	 *
	 * ZERO IS NOT A LEVEL, so it cannot be mistaken for one. A character that
	 * has neither gained a level nor loaded a save reads its level from
	 * `Cataclysm.PlayerLevel` instead, which is what every existing automation
	 * test and every existing call site expects.
	 */
	static constexpr int32 LevelNotYetDecided = 0;

	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Experience")
	int32 CharacterLevel = LevelNotYetDecided;

	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Experience")
	int64 ExperienceIntoLevel = 0;

	/**
	 * REPLICATED, because a client draws its own character sheet from this and
	 * the server is what decides whether a spend was legal.
	 */
	UPROPERTY(Replicated, VisibleAnywhere, Category = "Cataclysm|Attributes")
	FCataclysmAttributePoints SpentAttributePoints;

	UPROPERTY(VisibleAnywhere, Category = "Cataclysm|Abilities")
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystemComponent;

	/**
	 * A player carries all five sets. Enemies carry only the three that describe
	 * a combatant: they have no attribute points to spend and no class tree, so
	 * giving them the primary or class resource sets would be dead weight on
	 * every spawn.
	 */
	UPROPERTY()
	TObjectPtr<UCataclysmVitalAttributeSet> VitalAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmPrimaryAttributeSet> PrimaryAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmCombatAttributeSet> CombatAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmResistanceAttributeSet> ResistanceAttributes;

	UPROPERTY()
	TObjectPtr<UCataclysmClassResourceAttributeSet> ClassResourceAttributes;
};
