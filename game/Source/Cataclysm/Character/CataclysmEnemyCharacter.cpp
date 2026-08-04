// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyCharacter.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Half-height and radius of the collision capsule, in centimetres. */
	constexpr float EnemyCapsuleRadius = 48.0f;
	constexpr float EnemyCapsuleHalfHeight = 80.0f;

}

ACataclysmEnemyCharacter::ACataclysmEnemyCharacter()
{
	// Every enemy is on the Monsters side, so no skill of one enemy's can hit
	// another. The exception the design asks for -- Madness, where "the enemy
	// attacks anything nearby, friend or foe" -- is a change of attitude for a
	// tagged actor and not a change of side. Issue #163 builds it.
	TeamId = UCataclysmTeams::IdFor(ECataclysmTeam::Monsters);

	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Minimal replication: no client owns an enemy, so no client needs full
	// gameplay effect data for one. Tags and cues are enough to drive visuals.
	// A dungeon floor can hold a great many enemies and this is where the
	// bandwidth goes if it is set wrong.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	// Three sets, not five. An enemy has no attribute points to spend and no
	// class tree, so the primary attribute set and the class resource set would
	// be dead weight replicated on every spawn, and a dungeon floor can hold a
	// great many enemies.
	VitalAttributes = CreateDefaultSubobject<UCataclysmVitalAttributeSet>(TEXT("VitalAttributes"));
	CombatAttributes = CreateDefaultSubobject<UCataclysmCombatAttributeSet>(TEXT("CombatAttributes"));
	ResistanceAttributes = CreateDefaultSubobject<UCataclysmResistanceAttributeSet>(TEXT("ResistanceAttributes"));

	// A stand-in body, for the same reason the player has one: this project's
	// own Content folder holds no meshes at all, so without it an enemy is an
	// invisible capsule and there is no way to tell whether one is there.
	//
	// Slightly wider and shorter than the player's, so the two can be told
	// apart at a glance while both are cylinders.
	GetCapsuleComponent()->InitCapsuleSize(EnemyCapsuleRadius, EnemyCapsuleHalfHeight);

	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(RootComponent);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(
		(EnemyCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(EnemyCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(EnemyCapsuleHalfHeight * 2.0f) / ACataclysmCharacterBase::BasicShapeSize));

	// Found by path rather than referenced as an asset, because these are engine
	// content. A failure here is not fatal: the capsule is still there and still
	// takes damage, it is just invisible.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
	}
}

void ACataclysmEnemyCharacter::SetHealth(float NewMaxHealth)
{
	if (NewMaxHealth <= 0.0f)
	{
		return;
	}

	// REMEMBERED AS WELL AS APPLIED, and that is what makes the order of calls
	// not matter. A spawner naturally sets health on the line after SpawnActor,
	// which on an actor whose BeginPlay has not run yet is before the attribute
	// sets are registered -- so writing then is either lost or, worse, raises an
	// engine ensure. Storing it means InitAbilityActorInfo applies it when the
	// ability system is genuinely ready.
	StartingMaxHealth = NewMaxHealth;
	ApplyStartingHealth();
}

void ACataclysmEnemyCharacter::ApplyStartingHealth()
{
	if (!AbilitySystemComponent || StartingMaxHealth <= 0.0f)
	{
		return;
	}

	// Writing to an attribute the ability system does not hold yet raises an
	// engine ensure rather than failing quietly, so this is checked rather than
	// attempted. It is false before the component has been initialised.
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (!AbilitySystemComponent->HasAttributeSetForAttribute(MaxHealth))
	{
		return;
	}

	// MAXIMUM FIRST, THEN CURRENT, and the order is not incidental. The vital
	// attribute set clamps health to the maximum in PreAttributeChange, so
	// raising the current value before the maximum would clamp it straight back
	// down to whatever the old maximum was.
	AbilitySystemComponent->SetNumericAttributeBase(MaxHealth, StartingMaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(
		UCataclysmVitalAttributeSet::GetHealthAttribute(), StartingMaxHealth);
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

	// Now that the attribute sets are registered, whatever health a spawner
	// asked for before this point can finally be written.
	ApplyStartingHealth();

	if (HasAuthority() && StartingAbilitySet)
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystemComponent);
		StartingAbilitySet->GiveToAbilitySystem(AbilitySystemComponent, &GrantedHandles, this);
	}
}
