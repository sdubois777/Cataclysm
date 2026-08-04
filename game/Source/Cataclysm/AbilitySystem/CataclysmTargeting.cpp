// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

bool UCataclysmTargeting::IsInCone(const FVector& Origin, const FVector& Forward,
								   const FVector& Point, float RadiusCm,
								   float AngleDegrees)
{
	if (RadiusCm <= 0.0f)
	{
		return false;
	}

	FVector ToPoint = Point - Origin;
	ToPoint.Z = 0.0f;

	const float DistanceSquared = ToPoint.SizeSquared();
	if (DistanceSquared > RadiusCm * RadiusCm)
	{
		return false;
	}

	// A ring. Checked before normalising so that a target standing exactly on
	// the caster is inside a ring rather than falling out of it on a degenerate
	// direction vector.
	if (AngleDegrees >= 360.0f)
	{
		return true;
	}
	if (AngleDegrees <= 0.0f)
	{
		return false;
	}

	// Standing on the caster is inside any cone: there is no direction to
	// compare, and the alternative is that a point-blank enemy is immune.
	if (DistanceSquared <= UE_KINDA_SMALL_NUMBER)
	{
		return true;
	}

	FVector Facing = Forward;
	Facing.Z = 0.0f;
	if (!Facing.Normalize())
	{
		return false;
	}

	const float Cosine = FVector::DotProduct(Facing, ToPoint / FMath::Sqrt(DistanceSquared));

	// The written angle is the full width, so half of it is the limit either
	// side of the facing direction.
	return Cosine >= FMath::Cos(FMath::DegreesToRadians(AngleDegrees * 0.5f));
}

bool UCataclysmTargeting::IsInLine(const FVector& Start, const FVector& End,
								   const FVector& Point, float HalfWidthCm)
{
	if (HalfWidthCm <= 0.0f)
	{
		return false;
	}

	FVector Segment = End - Start;
	Segment.Z = 0.0f;

	FVector ToPoint = Point - Start;
	ToPoint.Z = 0.0f;

	const float LengthSquared = Segment.SizeSquared();
	if (LengthSquared <= UE_KINDA_SMALL_NUMBER)
	{
		// A line with no length is a circle at its start.
		return ToPoint.SizeSquared() <= HalfWidthCm * HalfWidthCm;
	}

	// Clamped, so a point past either end is measured from that end rather than
	// from an imaginary extension of the line. Without the clamp a charge would
	// hit an enemy standing well behind the character.
	const float Along = FMath::Clamp(
		FVector::DotProduct(ToPoint, Segment) / LengthSquared, 0.0f, 1.0f);

	const FVector Nearest = Segment * Along;
	return (ToPoint - Nearest).SizeSquared() <= HalfWidthCm * HalfWidthCm;
}

UAbilitySystemComponent* UCataclysmTargeting::AbilitySystemOf(const AActor* Actor)
{
	// Through the globals rather than by searching for a component, because the
	// player's ability system lives on the player state and not on the pawn.
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
}

bool UCataclysmTargeting::IsHostileTo(const AActor* Actor, const AActor* Instigator)
{
	if (!IsValid(Actor) || Actor == Instigator)
	{
		return false;
	}

	// It must be something that can hold damage at all. See the class comment:
	// this project has no faction concept, so having an ability system is most
	// of what makes something a target. Issue #162.
	if (!AbilitySystemOf(Actor))
	{
		return false;
	}

	if (!Instigator)
	{
		return true;
	}

	// OWNERSHIP STANDS IN FOR A SIDE, because there is nothing better to ask
	// yet. Without this a Ritualist's own imps are enemies to their own skills:
	// a summoned minion has an ability system and is not the caster, which is
	// the whole of the test above. Checked in both directions so that a minion's
	// own attack does not target the character that summoned it either.
	for (const AActor* Owner = Actor->GetOwner(); Owner; Owner = Owner->GetOwner())
	{
		if (Owner == Instigator)
		{
			return false;
		}
	}
	for (const AActor* Owner = Instigator->GetOwner(); Owner; Owner = Owner->GetOwner())
	{
		if (Owner == Actor)
		{
			return false;
		}
	}

	return true;
}

TArray<AActor*> UCataclysmTargeting::Gather(
	const UWorld* World, const AActor* Instigator, const FVector& Origin,
	float SearchRadiusCm, int32 MaxTargets,
	TFunctionRef<bool(const FVector&)> Predicate)
{
	TArray<AActor*> Found;
	if (!World || SearchRadiusCm <= 0.0f)
	{
		return Found;
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(CataclysmSkillTargets), /*bInTraceComplex=*/false);
	if (Instigator)
	{
		Query.AddIgnoredActor(Instigator);
	}

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps, Origin, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(SearchRadiusCm), Query);

	// One entry per actor. A character overlaps on both its capsule and its
	// mesh, so without this a single enemy is hit twice by one swing.
	TSet<AActor*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!IsHostileTo(Actor, Instigator) || Seen.Contains(Actor))
		{
			continue;
		}
		if (!Predicate(Actor->GetActorLocation()))
		{
			continue;
		}
		Seen.Add(Actor);
		Found.Add(Actor);
	}

	Found.Sort([&Origin](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), Origin)
			 < FVector::DistSquared(B.GetActorLocation(), Origin);
	});

	if (MaxTargets > 0 && Found.Num() > MaxTargets)
	{
		Found.SetNum(MaxTargets);
	}

	return Found;
}

TArray<AActor*> UCataclysmTargeting::FindEnemiesInSphere(
	const UWorld* World, const AActor* Instigator, const FVector& Origin,
	float RadiusCm, int32 MaxTargets)
{
	// The overlap has already applied the radius, so the predicate accepts
	// everything it returns.
	return Gather(World, Instigator, Origin, RadiusCm, MaxTargets,
				  [](const FVector&) { return true; });
}

TArray<AActor*> UCataclysmTargeting::FindEnemiesInCone(
	const UWorld* World, const AActor* Instigator, const FVector& Origin,
	const FVector& Forward, float RadiusCm, float AngleDegrees, int32 MaxTargets)
{
	return Gather(World, Instigator, Origin, RadiusCm, MaxTargets,
		[&](const FVector& Point)
		{
			return IsInCone(Origin, Forward, Point, RadiusCm, AngleDegrees);
		});
}

TArray<AActor*> UCataclysmTargeting::FindEnemiesInLine(
	const UWorld* World, const AActor* Instigator, const FVector& Start,
	const FVector& End, float HalfWidthCm, int32 MaxTargets)
{
	// Overlapped around the MIDDLE of the segment rather than around Start, so
	// one sphere covers the whole line. Its radius is half the length plus the
	// width, which is the smallest sphere that can.
	const FVector Middle = (Start + End) * 0.5f;
	const float SearchRadius = FVector::Dist(Start, End) * 0.5f + HalfWidthCm;

	TArray<AActor*> Found = Gather(World, Instigator, Middle, SearchRadius, /*MaxTargets=*/0,
		[&](const FVector& Point)
		{
			return IsInLine(Start, End, Point, HalfWidthCm);
		});

	// Re-sorted from the START of the line rather than from its middle, because
	// a piercing projectile has to hit in the order it passes through.
	Found.Sort([&Start](const AActor& A, const AActor& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), Start)
			 < FVector::DistSquared(B.GetActorLocation(), Start);
	});

	if (MaxTargets > 0 && Found.Num() > MaxTargets)
	{
		Found.SetNum(MaxTargets);
	}

	return Found;
}
