// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyCharacter.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmAttributeSet.h"

ACataclysmEnemyCharacter::ACataclysmEnemyCharacter()
{
	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Minimal replication: no client owns an enemy, so no client needs full
	// gameplay effect data for one. Tags and cues are enough to drive visuals.
	// A dungeon floor can hold a great many enemies and this is where the
	// bandwidth goes if it is set wrong.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	AttributeSet = CreateDefaultSubobject<UCataclysmAttributeSet>(TEXT("AttributeSet"));
}

UAbilitySystemComponent* ACataclysmEnemyCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACataclysmEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Owner and avatar are both this actor, so there is no possession or
	// replication ordering to wait for. BeginPlay is sufficient on both sides.
	InitAbilityActorInfo();
}

void ACataclysmEnemyCharacter::InitAbilityActorInfo()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(this, this);

	if (HasAuthority() && StartingAbilitySet)
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystemComponent);
		StartingAbilitySet->GiveToAbilitySystem(AbilitySystemComponent, &GrantedHandles, this);
	}
}
