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
	// NOT REPLICATED, though the class is by default: what is gathered here never leaves this machine.
	AbilitySystem->SetIsReplicated(false);

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
