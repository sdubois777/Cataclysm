// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Half-height and radius of the imp's collision capsule, in centimetres. */
	constexpr float MinionCapsuleRadius = 30.0f;
	constexpr float MinionCapsuleHalfHeight = 45.0f;

	/** How fast it walks, in centimetres per second. Faster than a monster,
	 *  because Summon Imp is written as "fast swarming melee". */
	constexpr float MinionWalkSpeedCmPerSecond = 500.0f;
}

ACataclysmMinion::ACataclysmMinion()
{
	// Nothing to do per frame. Its controller thinks on a timer and the
	// character movement component walks it.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// The same brain a monster has. What differs is its side and what its
	// attacks are worth, not how it decides.
	AIControllerClass = ACataclysmEnemyController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	AbilitySystemComponent = CreateDefaultSubobject<UCataclysmAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	VitalAttributes = CreateDefaultSubobject<UCataclysmVitalAttributeSet>(
		TEXT("VitalAttributes"));

	// SMALLER THAN AN ENEMY AND SMALLER THAN THE PLAYER, so that three of them
	// around a fight are recognisable as imps rather than as more monsters.
	GetCapsuleComponent()->InitCapsuleSize(MinionCapsuleRadius, MinionCapsuleHalfHeight);

	GetCharacterMovement()->MaxWalkSpeed = MinionWalkSpeedCmPerSecond;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 640.0f, 0.0f);

	// A stand-in body, for the same reason the player and every enemy have one:
	// this project's Content folder holds no meshes at all, so without it an imp
	// is an invisible capsule and there is no way to tell whether one is there.
	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(RootComponent);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetRelativeScale3D(FVector(
		(MinionCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(MinionCapsuleRadius * 2.0f) / ACataclysmCharacterBase::BasicShapeSize,
		(MinionCapsuleHalfHeight * 2.0f) / ACataclysmCharacterBase::BasicShapeSize));

	// A cone rather than the cylinder the player and enemies use, so an imp is
	// distinguishable from both at a glance. Engine content, found by path, so
	// this adds no asset to the project. A failure here is not fatal: the
	// capsule is still there and still takes damage, it is just invisible.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(
		TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (ConeMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(ConeMesh.Object);
	}
}

UAbilitySystemComponent* ACataclysmMinion::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ACataclysmMinion::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

ACataclysmMinion* ACataclysmMinion::Spawn(AActor* InSummoner, const FVector& Location,
										  float Lifetime, bool bBurns)
{
	if (!IsValid(InSummoner) || Lifetime <= 0.0f)
	{
		return nullptr;
	}

	UWorld* World = InSummoner->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	// OWNED BY THE SUMMONER, and that is load-bearing rather than tidiness.
	// UCataclysmTeams::TeamOf follows the owner chain, so ownership is what
	// keeps a summon on its summoner's side on a client, where the team assigned
	// below is a server-side value that is not itself replicated.
	SpawnParams.Owner = InSummoner;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACataclysmMinion* Minion = World->SpawnActor<ACataclysmMinion>(
		ACataclysmMinion::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (!Minion)
	{
		return nullptr;
	}

	Minion->Summoner = InSummoner;
	Minion->bBurnsWhatItHits = bBurns;

	// The summoner's side, not one of its own. A Ritualist's imps must be
	// friendly to a second player in the party, not merely to the Ritualist,
	// and ownership alone cannot say that.
	Minion->SetGenericTeamId(UCataclysmTeams::TeamOf(InSummoner));

	Minion->SetLifeSpan(Lifetime);

	return Minion;
}

void ACataclysmMinion::AttackOnce()
{
	// Nearest first, one target. Used by tests and by anything that wants one
	// swing without a controller; the ordinary case is the controller calling
	// AttackTarget with what it chose.
	const TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
		GetWorld(), this, GetActorLocation(), ReachCm, /*MaxTargets=*/1);
	if (Nearby.IsEmpty())
	{
		return;
	}

	AttackTarget(Nearby[0]);
}

void ACataclysmMinion::AttackTarget(AActor* Target)
{
	if (!IsValid(Summoner) || !IsValid(Target))
	{
		return;
	}

	// Damage comes from the SUMMONER's weapon, not the minion's own, which it
	// has none of. So a Ritualist's imps get stronger as the Ritualist does,
	// which is how every minion in the genre scales.
	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Summoner, Target, DamagePercentOfSummoner);

	if (bBurnsWhatItHits && Dealt > 0.0f)
	{
		UCataclysmSkillEffects::ApplyBurn(Summoner, Target, Dealt);
	}

	++AttacksMade;
}

void ACataclysmMinion::Explode(float RadiusCm, float DamagePercent)
{
	if (IsValid(Summoner) && RadiusCm > 0.0f && DamagePercent > 0.0f)
	{
		const TArray<AActor*> Caught = UCataclysmTargeting::FindEnemiesInSphere(
			GetWorld(), this, GetActorLocation(), RadiusCm);

		for (AActor* Target : Caught)
		{
			const float Dealt = UCataclysmSkillEffects::ApplyHit(
				Summoner, Target, DamagePercent);
			if (bBurnsWhatItHits && Dealt > 0.0f)
			{
				UCataclysmSkillEffects::ApplyBurn(Summoner, Target, Dealt);
			}
		}
	}

	Destroy();
}
