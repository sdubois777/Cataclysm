// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayerState.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmPrimaryAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"

ACataclysmPlayerState::ACataclysmPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed replication: the owning client receives full gameplay effect data,
	// while other clients see only the resulting tags and cues. Full is wasteful
	// for a player-controlled actor and Minimal loses information the owner's
	// own interface needs.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// Created as default subobjects rather than granted at runtime, because a
	// player has all five for its whole lifetime. An attribute set added later
	// does not retroactively replicate to clients that already have the
	// component, so the ones every player always carries are created here.
	VitalAttributes = CreateDefaultSubobject<UCataclysmVitalAttributeSet>(TEXT("VitalAttributes"));
	PrimaryAttributes = CreateDefaultSubobject<UCataclysmPrimaryAttributeSet>(TEXT("PrimaryAttributes"));
	CombatAttributes = CreateDefaultSubobject<UCataclysmCombatAttributeSet>(TEXT("CombatAttributes"));
	ResistanceAttributes = CreateDefaultSubobject<UCataclysmResistanceAttributeSet>(TEXT("ResistanceAttributes"));
	ClassResourceAttributes = CreateDefaultSubobject<UCataclysmClassResourceAttributeSet>(TEXT("ClassResourceAttributes"));

	// APlayerState replicates at 1 Hz by default, which is fine for a score but
	// far too slow for health bars and cooldowns driven off attributes.
	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* ACataclysmPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
