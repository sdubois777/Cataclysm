// Copyright Stephen Dubois. All Rights Reserved.

#include "Player/CataclysmPlayerState.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAttributeSet.h"

ACataclysmPlayerState::ACataclysmPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed replication: the owning client receives full gameplay effect data,
	// while other clients see only the resulting tags and cues. Full is wasteful
	// for a player-controlled actor and Minimal loses information the owner's
	// own interface needs.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UCataclysmAttributeSet>(TEXT("AttributeSet"));

	// APlayerState replicates at 1 Hz by default, which is fine for a score but
	// far too slow for health bars and cooldowns driven off attributes.
	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* ACataclysmPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
