// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

ACataclysmGroundZone::ACataclysmGroundZone()
{
	// Nothing to do per frame. It sweeps on a timer, a second apart, and a tick
	// would run it sixty times more often for the same result.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// See the header. Without a root component the actor has no position and
	// every zone sweeps around the world origin.
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);
}

ACataclysmGroundZone* ACataclysmGroundZone::Spawn(
	AActor* Owner, const FVector& Location, float RadiusCm, float Duration,
	float DamagePerTick)
{
	// A circle is a path whose two ends are the same point.
	return SpawnAlong(Owner, Location, Location, RadiusCm, Duration, DamagePerTick);
}

ACataclysmGroundZone* ACataclysmGroundZone::SpawnAlong(
	AActor* Owner, const FVector& Start, const FVector& End, float HalfWidthCm,
	float Duration, float DamagePerTick)
{
	if (!IsValid(Owner) || HalfWidthCm <= 0.0f || Duration <= 0.0f)
	{
		return nullptr;
	}

	const FVector Location = Start;
	const float RadiusCm = HalfWidthCm;

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Owner;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACataclysmGroundZone* Zone = World->SpawnActor<ACataclysmGroundZone>(
		ACataclysmGroundZone::StaticClass(), Location, FRotator::ZeroRotator,
		SpawnParams);
	if (!Zone)
	{
		return nullptr;
	}

	Zone->RadiusCm = RadiusCm;
	Zone->DamagePerTick = DamagePerTick;

	// Read back from the actor rather than trusting Start, because AlwaysSpawn
	// still lets the engine adjust a spawn position, and the near end has to be
	// the position the actor actually ended up at or the two ends disagree.
	Zone->FarEnd = Zone->GetActorLocation() + (End - Start);

	Zone->SetLifeSpan(Duration);

	return Zone;
}

void ACataclysmGroundZone::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		// First sweep a full tick in rather than at once, so that a zone left by
		// a skill that already hit everyone standing there does not hit them
		// twice in the same instant.
		World->GetTimerManager().SetTimer(
			SweepTimer, this, &ACataclysmGroundZone::Sweep,
			TickSeconds, /*bLoop=*/true, /*InFirstDelay=*/TickSeconds);
	}
}

void ACataclysmGroundZone::Sweep()
{
	// Named Source rather than Instigator: AActor already has a member of that
	// name, and shadowing it is an error at this project's warning level.
	AActor* Source = GetOwner();
	if (!IsValid(Source) || DamagePerTick <= 0.0f)
	{
		LastSweepCount = 0;
		return;
	}

	// Asked afresh every sweep. Standing in it is the cost, so who is inside has
	// to be a question about now rather than about when it was created.
	//
	// ONE SEARCH FOR BOTH SHAPES. FindEnemiesInLine with two ends at the same
	// point is a circle of RadiusCm at that point, because IsInLine treats a
	// segment of no length that way, so a round zone and a long one cannot drift
	// apart in behaviour.
	const TArray<AActor*> Inside = UCataclysmTargeting::FindEnemiesInLine(
		GetWorld(), Source, GetActorLocation(), FarEnd, RadiusCm);

	for (AActor* Target : Inside)
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Source, Target, DamagePerTick);
	}

	LastSweepCount = Inside.Num();
	++TicksElapsed;
}
