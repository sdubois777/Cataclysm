// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmComparisonStandIn.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Engine/World.h"

ACataclysmComparisonStandIn::ACataclysmComparisonStandIn()
{
	// NOTHING HAPPENS TO IT OVER TIME: it is gathered onto when a tooltip is built, and read at once.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetHidden(true);
	SetCanBeDamaged(false);

	AbilitySystem = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	// NOT REPLICATED, AND NOTHING NEEDS SAYING TO THE COMPONENT FOR IT: a component replicates only through its actor,
	// and `bReplicates` is false above, so what is gathered here never leaves this machine.
	//
	// NOT `AbilitySystem->SetIsReplicated(false)`, which this constructor called until it was compiled (issue #832
	// phase 2). The class sets true, so a call asking for false runs the body, and in a constructor that body's
	// ensure "SetIsReplicatedByDefault is preferred during Component Construction" fires (`UActorComponent::
	// SetIsReplicated` asserts `!NeedsInitialization()`). `SetIsReplicatedByDefault` is protected. The other actors
	// here that call `SetIsReplicated` ask for the true the class already holds, so their bodies never run.

	// THE PLAYER STATE'S FIVE, AS DEFAULT SUBOBJECTS, so the component finds them when it initialises, as the player
	// state's own component finds its five.
	VitalAttributes = CreateDefaultSubobject<UCataclysmVitalAttributeSet>(TEXT("VitalAttributes"));
	PrimaryAttributes = CreateDefaultSubobject<UCataclysmPrimaryAttributeSet>(TEXT("PrimaryAttributes"));
	CombatAttributes = CreateDefaultSubobject<UCataclysmCombatAttributeSet>(TEXT("CombatAttributes"));
	ResistanceAttributes = CreateDefaultSubobject<UCataclysmResistanceAttributeSet>(TEXT("ResistanceAttributes"));
	ClassResourceAttributes =
		CreateDefaultSubobject<UCataclysmClassResourceAttributeSet>(TEXT("ClassResourceAttributes"));
}

void ACataclysmComparisonStandIn::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (AbilitySystem)
	{
		AbilitySystem->InitAbilityActorInfo(this, this);
	}
}

ACataclysmComparisonStandIn* ACataclysmComparisonStandIn::SpawnIn(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// TRANSIENT, SO NO SAVE AND NO LEVEL EVER KEEPS IT.
	Params.ObjectFlags |= RF_Transient;
	return World->SpawnActor<ACataclysmComparisonStandIn>(Params);
}
