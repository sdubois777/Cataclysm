// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmGroundEffect.h"
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
	float Duration, float DamagePerTick, bool bBurnsEveryone)
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
	Zone->bBurnsEveryone = bBurnsEveryone;

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

	// DRAWN, WHICH UNTIL ISSUE #811 IT WAS NOT. Every patch of burning ground in
	// the game was invisible: this class had a scene component and nothing else,
	// and its own header said so. Eight of the sixteen designed Demonic skills
	// leave one, and a player could stand in any of them and see nothing.
	//
	// HERE RATHER THAN IN Spawn AND SpawnAlong, because both of those end at the
	// same actor and doing it once is one place to get wrong instead of two. It
	// also means a zone placed in a level by hand draws as well as one a skill
	// left.
	//
	// THE COLOUR COMES FROM THE OWNER AND IS NAME_None FOR A PLAYER, so a zone a
	// player leaves draws the system's authored white. That is issue #803 and
	// not a fault here: a zone carries no skill tags of its own to read an
	// Element.* tag from, unlike UCataclysmStrikeSkill which does.
	UCataclysmGroundEffect::PlayFor(this, GetActorLocation(), FarEnd, RadiusCm,
									GetLifeSpan(),
									UCataclysmSkillEffects::DamageTypeOf(GetOwner()));

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
	// WHICH SEARCH DEPENDS ON WHOSE FIRE IT IS. Almost every zone belongs to
	// whoever cast it and burns the other side. The Hellhound's lane burns
	// whatever is standing in it, the Hellhound included, which is the one
	// thing in the design that asks for it.
	const TArray<AActor*> Inside = bBurnsEveryone
		? UCataclysmTargeting::FindEveryoneInLine(
			GetWorld(), Source, GetActorLocation(), FarEnd, RadiusCm)
		: UCataclysmTargeting::FindEnemiesInLine(
			GetWorld(), Source, GetActorLocation(), FarEnd, RadiusCm);

	for (AActor* Target : Inside)
	{
		// AREA AND OVER TIME BOTH. A zone catches whatever is standing in it
		// rather than striking one target, so it cannot be evaded; and it is
		// damage over time, so an energy shield does not absorb it. Issue #513.
		FCataclysmHitDelivery Delivery;
		Delivery.bIsArea = true;
		Delivery.bIsDamageOverTime = true;
		UCataclysmSkillEffects::ApplyDirectDamage(Source, Target, DamagePerTick,
												  Delivery);
	}

	LastSweepCount = Inside.Num();
	++TicksElapsed;
}
