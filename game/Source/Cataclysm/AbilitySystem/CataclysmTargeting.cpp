// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
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

bool UCataclysmTargeting::MatchesAttitude(const AActor* Actor, const AActor* Instigator,
										  ETeamAttitude::Type Wanted)
{
	if (!IsValid(Actor) || Actor == Instigator)
	{
		return false;
	}

	// It must be something that can hold damage at all, whichever side it is on.
	// This is what keeps scenery out of both searches: a wall is not an enemy,
	// and it is not an ally an aura can buff either.
	if (!AbilitySystemOf(Actor))
	{
		return false;
	}

	// A DEAD CHARACTER IS NEITHER AN ENEMY NOR AN ALLY. Asked here rather than
	// in each of the four Find functions, because this is the one place both
	// questions are already asked together.
	//
	// WHAT IT FIXES IS MEASURED, NOT HYPOTHETICAL. Issue #570 recorded a play
	// session in which fifty-six attacks over seventy seconds landed on a player
	// whose health had already reached zero, each dealing exactly nothing,
	// because the creature attacking had no way to ask whether its target was
	// still worth attacking. It applies to the enemy side as well, for the one
	// tick a corpse exists before it is destroyed.
	//
	// A DEAD CHARACTER CAN STILL BE THE INSTIGATOR of something already in
	// flight -- a projectile outlives the caster -- so this filters what a search
	// FINDS and says nothing about who ran it.
	if (UCataclysmSkillEffects::IsDead(Actor))
	{
		return false;
	}

	// No instigator means no side to compare against. A hostile search then
	// returns everything, which is what a hazard belonging to nobody should do;
	// a friendly search returns nothing, because nothing can be an ally of no
	// one.
	if (!Instigator)
	{
		return Wanted == ETeamAttitude::Hostile;
	}

	return UCataclysmTeams::AttitudeBetween(Instigator, Actor) == Wanted;
}

bool UCataclysmTargeting::IsHostileTo(const AActor* Actor, const AActor* Instigator)
{
	return MatchesAttitude(Actor, Instigator, ETeamAttitude::Hostile);
}

bool UCataclysmTargeting::IsFriendlyTo(const AActor* Actor, const AActor* Instigator)
{
	return MatchesAttitude(Actor, Instigator, ETeamAttitude::Friendly);
}

TArray<AActor*> UCataclysmTargeting::FindEveryoneInLine(
	const UWorld* World, const AActor* Origin, const FVector& Start,
	const FVector& End, float HalfWidthCm)
{
	// THE SAME SPHERE FindEnemiesInLine USES, around the middle of the segment
	// rather than around Start, so one overlap covers the whole line.
	const FVector Middle = (Start + End) * 0.5f;
	const float SearchRadius = FVector::Dist(Start, End) * 0.5f + HalfWidthCm;

	return Gather(World, Origin, Middle, SearchRadius, /*MaxTargets=*/0,
		ETeamAttitude::Hostile,
		[&](const FVector& Point)
		{
			return IsInLine(Start, End, Point, HalfWidthCm);
		},
		/*bEveryone=*/true);
}

TArray<AActor*> UCataclysmTargeting::Gather(
	const UWorld* World, const AActor* Instigator, const FVector& Origin,
	float SearchRadiusCm, int32 MaxTargets, ETeamAttitude::Type Wanted,
	TFunctionRef<bool(const FVector&)> Predicate, bool bEveryone)
{
	TArray<AActor*> Found;
	if (!World || SearchRadiusCm <= 0.0f)
	{
		return Found;
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(CataclysmSkillTargets), /*bInTraceComplex=*/false);
	if (Instigator && !bEveryone)
	{
		// A SEARCH THAT TAKES NO SIDE KEEPS THE INSTIGATOR IN. The Hellhound's
		// burning lane is the one thing that must be able to reach the creature
		// that made it, so the collision query cannot be told to ignore it.
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

		// TAKING NO SIDE STILL REFUSES SCENERY AND THE DEAD, which is what
		// MatchesAttitude asks first and what a bare `bEveryone` would have
		// dropped along with the question of sides.
		const bool bWanted = bEveryone
			? (IsValid(Actor) && AbilitySystemOf(Actor) != nullptr
			   && !UCataclysmSkillEffects::IsDead(Actor))
			: MatchesAttitude(Actor, Instigator, Wanted);

		if (!bWanted || Seen.Contains(Actor))
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
	return Gather(World, Instigator, Origin, RadiusCm, MaxTargets, ETeamAttitude::Hostile,
				  [](const FVector&) { return true; });
}

TArray<AActor*> UCataclysmTargeting::FindAlliesInSphere(
	const UWorld* World, const AActor* Instigator, const FVector& Origin,
	float RadiusCm, int32 MaxTargets)
{
	return Gather(World, Instigator, Origin, RadiusCm, MaxTargets, ETeamAttitude::Friendly,
				  [](const FVector&) { return true; });
}

TArray<AActor*> UCataclysmTargeting::FindEnemiesInCone(
	const UWorld* World, const AActor* Instigator, const FVector& Origin,
	const FVector& Forward, float RadiusCm, float AngleDegrees, int32 MaxTargets)
{
	return Gather(World, Instigator, Origin, RadiusCm, MaxTargets, ETeamAttitude::Hostile,
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
		ETeamAttitude::Hostile,
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
