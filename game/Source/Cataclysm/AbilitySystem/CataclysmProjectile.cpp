// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "Character/CataclysmCharacterBase.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * A projectile with less than this left to travel has arrived.
	 *
	 * Compared against the remaining range rather than against zero so that
	 * floating point cannot leave one with a hundredth of a centimetre to go and
	 * another step to take.
	 */
	constexpr float CataclysmProjectileArrivedWithinCm = 0.1f;

	/**
	 * A cap on how much of one frame a single step may cover.
	 *
	 * A step is swept, so a long one is not a correctness problem in itself: the
	 * capsule still covers every place the projectile passed. It is a problem
	 * for the ORDER of what it passed. A projectile that does not pierce should
	 * stop at the first enemy and then not exist for the second, and a step long
	 * enough to contain both makes that ordering a property of the sweep rather
	 * than of time. A tenth of a second at the fastest designed speed -- Chain
	 * of Coals at 2000 centimetres per second -- is two metres.
	 */
	constexpr float CataclysmProjectileLongestStepSeconds = 0.1f;

	/**
	 * How long a finished projectile hangs about before being destroyed.
	 *
	 * NOT DESTROYED OUTRIGHT, so that whatever is listening on OnFinished can
	 * still read where it stopped. Short enough that a real game never has one
	 * of these on screen for a frame that matters.
	 */
	constexpr float CataclysmProjectileLingerSeconds = 0.01f;
}

ACataclysmProjectile::ACataclysmProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// NOT DECORATION, and this project has been caught by its absence twice. An
	// actor whose components are all non-scene components has no root component,
	// and an actor with no root component reports its location as the world
	// origin however it was spawned.
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	SetRootComponent(Anchor);

	// SOMETHING TO SEE. Until this, every projectile in the game was invisible:
	// the actor's only component was the anchor above. Scaled in Fire, once
	// BodyRadiusCm is known -- a piercing skill's projectile is as wide as the
	// line it hits along, and everything else uses the standard body width.
	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(Anchor);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Engine content, found by path, so this adds no asset to the project. A
	// sphere rather than the cylinder and cone the characters use, so a thing in
	// the air is not mistaken for a thing standing on the ground.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(SphereMesh.Object);
	}
}

ACataclysmProjectile* ACataclysmProjectile::Fire(
	AActor* Instigator, const FVector& From, const FVector& To, float InRadiusCm,
	float InSpeed, int32 InPierce, bool bInReturns, float InDamagePercent,
	const FGameplayTagContainer& InSkillTags, bool bInBurns)
{
	UWorld* World = Instigator ? Instigator->GetWorld() : nullptr;
	if (!World || InSpeed <= 0.0f || InRadiusCm <= 0.0f)
	{
		// A speed of zero is a beam, not a projectile. Infernal Lance is written
		// that way and arrives at once, which the firing skill resolves itself.
		return nullptr;
	}

	FVector Along = To - From;
	Along.Z = 0.0f;
	const float RangeCm = Along.Size();
	if (RangeCm <= CataclysmProjectileArrivedWithinCm)
	{
		// Aimed at its own feet, which is what happens with no cursor. There is
		// no path to fly along, so the caller resolves it as a landing instead.
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Instigator;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACataclysmProjectile* Projectile = World->SpawnActor<ACataclysmProjectile>(
		From, Along.Rotation(), SpawnParams);
	if (!Projectile)
	{
		return nullptr;
	}

	Projectile->StartedAt = From;
	Projectile->FurthestReached = From;
	Projectile->Direction = Along / RangeCm;
	Projectile->RadiusCm = InRadiusCm;
	Projectile->SpeedCmPerSecond = InSpeed;
	Projectile->RemainingRangeCm = RangeCm;
	Projectile->bPierces = InPierce > 0;
	Projectile->PiercesLeft = FMath::Max(0, InPierce);

	// A piercing skill's Radius is the half-width of the line it hits along, so
	// for one of those the flying object IS that wide. One that does not pierce
	// has a Radius that means the blast where it stops, so the object gets the
	// standard body width instead. See the comments on both fields.
	Projectile->BodyRadiusCm =
		Projectile->bPierces ? InRadiusCm : DefaultBodyRadiusCm;

	// SIZED TO WHAT IT ACTUALLY HITS WITH, so what the player sees and what the
	// sweep uses are the same width rather than two numbers that can disagree.
	// The engine's basic shapes are BasicShapeSize across, so a scale of 1 is
	// that wide and a body of BodyRadiusCm needs twice its radius over that.
	if (Projectile->PlaceholderBody)
	{
		const float Scale = (Projectile->BodyRadiusCm * 2.0f)
			/ ACataclysmCharacterBase::BasicShapeSize;
		Projectile->PlaceholderBody->SetRelativeScale3D(
			FVector(Scale, Scale, Scale));
	}
	Projectile->bWillReturn = bInReturns;
	Projectile->DamagePercent = InDamagePercent;
	Projectile->SkillTags = InSkillTags;
	Projectile->bBurns = bInBurns;

	UE_LOG(LogCataclysm, Verbose,
		TEXT("A projectile left %s at %.0f cm/s for %.0f cm, piercing %d."),
		*GetNameSafe(Instigator), InSpeed, RangeCm, InPierce);

	return Projectile;
}

void ACataclysmProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Step(DeltaSeconds);
}

bool ACataclysmProjectile::Step(float DeltaSeconds)
{
	if (bFinished || DeltaSeconds <= 0.0f)
	{
		return false;
	}

	// A long frame is taken as several steps rather than one, so that what a
	// projectile passes through, it passes through in order. See
	// CataclysmProjectileLongestStepSeconds.
	float SecondsLeft = DeltaSeconds;
	while (SecondsLeft > 0.0f && !bFinished)
	{
		const float ThisStep =
			FMath::Min(SecondsLeft, CataclysmProjectileLongestStepSeconds);
		SecondsLeft -= ThisStep;

		const FVector Previous = GetActorLocation();
		const float WantedCm = FMath::Min(SpeedCmPerSecond * ThisStep,
										  RemainingRangeCm);
		const FVector Wanted = Previous + Direction * WantedCm;

		// WORLD GEOMETRY FIRST, and only then who was standing there. A wall
		// between the caster and an enemy behind it has to stop the projectile
		// at the wall, so the sweep for enemies runs over the part of the step
		// that actually happened rather than the part that was asked for.
		const FVector Reached = TraceStep(Previous, Wanted);

		SetActorLocation(Reached);
		RemainingRangeCm -= FVector::Dist(Previous, Reached);

		// Tracked as it goes, so that a projectile which turns round still
		// knows how far out it got. That is what the burning ground it leaves
		// is measured along.
		if (!bReturning)
		{
			FurthestReached = Reached;
		}

		if (HitAlongStep(Previous, Reached) || bFinished)
		{
			// Stopped on an enemy, or ran out of pierces part way through.
			Finish();
			return false;
		}
		if (bBlockedByGeometry)
		{
			Finish();
			return false;
		}
		if (RemainingRangeCm <= CataclysmProjectileArrivedWithinCm)
		{
			if (bWillReturn && !bReturning)
			{
				BeginReturn();
				continue;
			}
			Finish();
			return false;
		}
	}

	return !bFinished;
}

FVector ACataclysmProjectile::TraceStep(const FVector& From, const FVector& To)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return To;
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(CataclysmProjectileFlight),
								/*bInTraceComplex=*/false);
	Query.AddIgnoredActor(this);
	if (AActor* Firer = GetOwner())
	{
		Query.AddIgnoredActor(Firer);
	}

	// THE VISIBILITY CHANNEL, because it is the one that answers "does something
	// solid stand between these two points". Pawns deliberately do not block it,
	// so one character never counts as cover for the character behind them: who
	// a projectile passes through is decided by Pierce, not by collision.
	FHitResult Blocked;
	if (World->LineTraceSingleByChannel(Blocked, From, To, ECC_Visibility, Query))
	{
		bBlockedByGeometry = true;
		return Blocked.ImpactPoint;
	}
	return To;
}

bool ACataclysmProjectile::HitAlongStep(const FVector& Previous,
										const FVector& Current)
{
	AActor* Firer = GetOwner();
	if (!Firer)
	{
		return false;
	}

	// THE CAPSULE FROM WHERE IT WAS TO WHERE IT IS, not a sphere at where it is.
	// That is the volume the projectile actually swept, so nothing thin enough
	// to sit between two steps is missed. FindEnemiesInLine returns them sorted
	// from the start of the segment, which is the order the projectile reached
	// them and therefore the order a non-piercing one must consider them in.
	const TArray<AActor*> Along = UCataclysmTargeting::FindEnemiesInLine(
		GetWorld(), Firer, Previous, Current, BodyRadiusCm);

	for (AActor* Target : Along)
	{
		if (AlreadyHit.Contains(Target))
		{
			continue;
		}
		AlreadyHit.Add(Target);

		if (!bPierces)
		{
			// It stops here. The hit itself happens in Finish, which detonates
			// in a radius and catches this target along with anything standing
			// near it, so hitting it now as well would hit it twice.
			return true;
		}

		HitOne(Target);

		--PiercesLeft;
		if (PiercesLeft <= 0)
		{
			// Out of pierces. It has already hit this one on the way through, so
			// it stops here, and a piercing projectile does not detonate.
			bFinished = true;
			return false;
		}
	}

	return false;
}

void ACataclysmProjectile::HitOne(AActor* Target)
{
	AActor* Firer = GetOwner();
	if (!Firer || !Target)
	{
		return;
	}

	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Firer, Target, DamagePercent, SkillTags);
	if (Dealt > 0.0f)
	{
		++EnemiesHit;

		// A burn is a share of the hit that caused it, so a projectile dealing
		// nothing sets nothing alight. That is the rule every other template
		// uses, and it is why a Support skill lights nothing.
		if (bBurns)
		{
			UCataclysmSkillEffects::ApplyBurn(Firer, Target, Dealt);
		}
	}
}

void ACataclysmProjectile::BeginReturn()
{
	bReturning = true;
	Direction = -Direction;
	RemainingRangeCm = FVector::Dist(GetActorLocation(), StartedAt);

	// CLEARED, because Emberhurl "hits once going out and once returning to your
	// hand". Everything it passed on the way out is a legal target again on the
	// way back.
	AlreadyHit.Reset();

	SetActorRotation(Direction.Rotation());
}

void ACataclysmProjectile::Finish()
{
	if (bFinishedAndSettled)
	{
		return;
	}
	bFinishedAndSettled = true;
	bFinished = true;
	SetActorTickEnabled(false);

	// A PROJECTILE THAT DOES NOT PIERCE DETONATES WHERE IT STOPPED, and where it
	// stopped is now a real place rather than where it was aimed. Blood Pyre and
	// Magma Quake go off against the first thing they touch, against the wall
	// that blocked them, or at the end of the throw if they touch nothing.
	if (!bPierces)
	{
		if (AActor* Firer = GetOwner())
		{
			for (AActor* Target : UCataclysmTargeting::FindEnemiesInSphere(
					GetWorld(), Firer, GetActorLocation(), RadiusCm))
			{
				HitOne(Target);
			}
		}
	}

	OnFinished.Broadcast(this);

	// Kept for a moment rather than destroyed here, so that whatever listened
	// can still read where it stopped in order to leave burning ground there.
	SetLifeSpan(CataclysmProjectileLingerSeconds);
}
