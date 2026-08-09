// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmEnemyCharacter.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmResistanceAttributeSet.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	// Its own brain, possessed as soon as it exists. Without both of these an
	// enemy stands where it was spawned for its whole life: nothing else in the
	// project gives a non-player pawn a controller.
	AIControllerClass = ACataclysmEnemyController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// It turns to face where it is walking, like the player character does.
	// Without this the cylinder slides sideways and there is no way to tell from
	// looking at it which way it thinks it is going.
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 480.0f, 0.0f);

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
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::SetAttackDamage(float NewAttackDamage)
{
	if (NewAttackDamage < 0.0f)
	{
		return;
	}

	StartingAttackDamage = NewAttackDamage;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::ApplyStartingAttributes()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	// Writing to an attribute the ability system does not hold yet raises an
	// engine ensure rather than failing quietly, so each is checked rather than
	// attempted. HasAttributeSetForAttribute is false before the component has
	// been initialised.
	const FGameplayAttribute MaxHealth =
		UCataclysmVitalAttributeSet::GetMaxHealthAttribute();
	if (StartingMaxHealth > 0.0f
		&& AbilitySystemComponent->HasAttributeSetForAttribute(MaxHealth))
	{
		// MAXIMUM FIRST, THEN CURRENT, and the order is not incidental. The vital
		// attribute set clamps health to the maximum in PreAttributeChange, so
		// raising the current value before the maximum would clamp it straight
		// back down to whatever the old maximum was.
		AbilitySystemComponent->SetNumericAttributeBase(MaxHealth, StartingMaxHealth);
		AbilitySystemComponent->SetNumericAttributeBase(
			UCataclysmVitalAttributeSet::GetHealthAttribute(), StartingMaxHealth);
	}

	const FGameplayAttribute Damage =
		UCataclysmCombatAttributeSet::GetAttackDamageAttribute();
	if (StartingAttackDamage > 0.0f
		&& AbilitySystemComponent->HasAttributeSetForAttribute(Damage))
	{
		AbilitySystemComponent->SetNumericAttributeBase(Damage, StartingAttackDamage);
	}

	// --- the rest of the designed stat block. Issue #372 ---
	//
	// UNTIL THIS LANDED AN ENEMY HELD THREE ATTRIBUTES OUT OF ROUGHLY TWENTY.
	// Armour, every resistance, evasion and both crit figures sat at the
	// attribute sets' own defaults and nothing ever wrote to them, so the Brute
	// -- which the design document calls heavily armoured and gives the
	// second-highest armour share of the seven vertical slice enemies -- was a
	// slow enemy with extra health and nothing else.
	//
	// WRITTEN DIRECTLY RATHER THAN THROUGH A GameplayEffect, which was the open
	// question on that issue. These are BASE values, and a modifier layers on
	// top of the base either way, so the effect's one real advantage does not
	// apply here. It would need an asset per enemy or a programmatic effect, and
	// issue #355 rebuilds this transport anyway once the archetype numbers are
	// game data. Two mechanisms for one job is worse than one.
	//
	// NO ZERO CHECKS BELOW, unlike the two writes above. Zero armour, zero
	// resistance and zero evasion are all designed values -- the Imp's armour
	// share really is 0.0 -- so treating zero as "not configured" would make an
	// unarmoured creature impossible to express.
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetArmorAttribute(), StartingArmour);
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetEvasionAttribute(), EvasionPercent);
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetCritChanceAttribute(),
				CritChancePercent);
	ApplyIfHeld(UCataclysmCombatAttributeSet::GetCritMultiplierAttribute(),
				CritMultiplierPercent);

	// ONE FIGURE ONTO ALL EIGHT. The design model says so in as many words:
	// "percent of all incoming damage resisted, whatever its type. One figure,
	// not eight." A per-type enemy profile is not something the design has, and
	// inventing one here would be inventing design.
	ApplyIfHeld(UCataclysmResistanceAttributeSet::GetWarResistanceAttribute(),
				ResistancePercent);
	ApplyIfHeld(UCataclysmResistanceAttributeSet::GetDemonicResistanceAttribute(),
				ResistancePercent);
	ApplyIfHeld(UCataclysmResistanceAttributeSet::GetDeathResistanceAttribute(),
				ResistancePercent);
	ApplyIfHeld(UCataclysmResistanceAttributeSet::GetPestilenceResistanceAttribute(),
				ResistancePercent);
	ApplyIfHeld(UCataclysmResistanceAttributeSet::GetFamineResistanceAttribute(),
				ResistancePercent);
	ApplyIfHeld(UCataclysmResistanceAttributeSet::GetCelestialResistanceAttribute(),
				ResistancePercent);
	ApplyIfHeld(UCataclysmResistanceAttributeSet::GetChaosResistanceAttribute(),
				ResistancePercent);
	ApplyIfHeld(UCataclysmResistanceAttributeSet::GetVoidResistanceAttribute(),
				ResistancePercent);
}

void ACataclysmEnemyCharacter::ApplyIfHeld(const FGameplayAttribute& Attribute,
										   float Value)
{
	// THE CHECK IS THE WHOLE POINT OF THE HELPER. Writing to an attribute the
	// ability system does not hold yet raises an engine ensure rather than
	// failing quietly, and HasAttributeSetForAttribute is false until the
	// component has been initialised. ApplyStartingAttributes runs both from the
	// setters and from InitAbilityActorInfo, so it really does run before that
	// sometimes.
	if (AbilitySystemComponent
		&& AbilitySystemComponent->HasAttributeSetForAttribute(Attribute))
	{
		AbilitySystemComponent->SetNumericAttributeBase(Attribute, Value);
	}
}

void ACataclysmEnemyCharacter::SetArmour(float NewArmour)
{
	if (NewArmour < 0.0f)
	{
		return;
	}

	StartingArmour = NewArmour;
	ApplyStartingAttributes();
}

void ACataclysmEnemyCharacter::AttackTarget(AActor* Target)
{
	// The same path a player's skill takes: written into the Damage meta
	// attribute and resolved through the full mitigation order. An enemy with no
	// attack damage set deals nothing and says so once, which ApplyHit handles.
	UCataclysmSkillEffects::ApplyHit(this, Target, AttackPercentOfOwnDamage);
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

	// Now that the attribute sets are registered, whatever health and attack
	// damage a spawner asked for before this point can finally be written.
	ApplyStartingAttributes();

	if (HasAuthority() && StartingAbilitySet)
	{
		GrantedHandles.TakeFromAbilitySystem(AbilitySystemComponent);
		StartingAbilitySet->GiveToAbilitySystem(AbilitySystemComponent, &GrantedHandles, this);
	}
}
