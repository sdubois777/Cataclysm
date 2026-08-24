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

protected:
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
