// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CataclysmComparisonStandIn.generated.h"

class UCataclysmAbilitySystemComponent;
class UCataclysmClassResourceAttributeSet;
class UCataclysmCombatAttributeSet;
class UCataclysmPrimaryAttributeSet;
class UCataclysmResistanceAttributeSet;
class UCataclysmVitalAttributeSet;
class UWorld;

/**
 * An actor that exists to have its stats worked out, and nothing else. Issue #832: a carried item's tooltip says
 * which of the character's totals would change if it were worn, and the totals are gathered on this rather than on
 * the character.
 *
 * WHY NOT THE CHARACTER ITSELF. The only place every stat source is gathered is
 * `UCataclysmEquipmentComponent::RefreshAttributesAs` (issue #1054), and it WRITES what it gathers onto the ability
 * system it is handed: the attributes, the stat inputs a skill reads, the worn rows' pool actions. Running it on the
 * player's own ability system to ask "what if" would change the player for a moment, and a second gatherer written
 * to answer without writing would one day disagree with the first. So the comparison runs the one gatherer on this.
 *
 * WHAT IT CARRIES: an ability system component and the player's five attribute sets, the five
 * `ACataclysmPlayerState` creates, as default subobjects so the component finds them when it initialises. ALL FIVE,
 * because `UCataclysmPlayerClassStats::ApplyTo` skips a stat whose set is missing, and a stand-in missing one would
 * read nought where the player reads a figure.
 *
 * IT TAKES NO PART IN PLAY. Hidden, not replicated, never ticks, has no collision and no primitive, so no overlap
 * that finds a target finds it, and nothing damages it. It is spawned transient by `SpawnIn`, never saved, and its
 * one user, `UCataclysmInventoryWidget`, destroys it with the screen.
 */
UCLASS(NotBlueprintable, Transient)
class CATACLYSM_API ACataclysmComparisonStandIn : public AActor
{
	GENERATED_BODY()

public:
	ACataclysmComparisonStandIn();

	/** The ability system the comparison gathers onto. */
	UCataclysmAbilitySystemComponent* GetCataclysmAbilitySystem() const { return AbilitySystem; }

	/**
	 * Spawns one into a world, transient and hidden.
	 *
	 * @return null with no world, or when the spawn is refused
	 */
	static ACataclysmComparisonStandIn* SpawnIn(UWorld* World);

protected:
	/**
	 * Gives the ability system its actor info, owner and avatar both this actor. HERE AND NOT LATER, because the
	 * component has found its attribute sets by now (`InitializeComponent`) and `InitAbilityActorInfo` with no actor
	 * asserts rather than failing politely, which the equipment tests' fixture records.
	 */
	virtual void PostInitializeComponents() override;

private:
	UPROPERTY()
	TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem;

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
