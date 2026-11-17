// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmShoulderThrough.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Engine/World.h"

const TCHAR* UCataclysmShoulderThrough::Stat = TEXT("moving_into_enemy_pushes_aside");

namespace
{
	/** How far out to look before measuring each body: generous, since each
	 *  body's own radius is added when it is measured. */
	constexpr float SearchCm = 400.0f;
}

AActor* UCataclysmShoulderThrough::EnemyMovedInto(const AActor* Character,
												  const FVector& Direction)
{
	FVector Along = Direction;
	Along.Z = 0.0f;
	if (!Character || Along.IsNearlyZero())
	{
		return nullptr;
	}
	Along.Normalize();

	const FVector From = Character->GetActorLocation();
	const float MineCm = Character->GetSimpleCollisionRadius();
	const float Cone = FMath::Cos(FMath::DegreesToRadians(ConeDegrees));

	AActor* Nearest = nullptr;
	float NearestCm = TNumericLimits<float>::Max();
	for (AActor* Enemy : UCataclysmTargeting::FindEnemiesInSphere(
			 Character->GetWorld(), Character, From, SearchCm))
	{
		FVector Toward = Enemy->GetActorLocation() - From;
		Toward.Z = 0.0f;
		const float ApartCm = Toward.Size();

		// TOUCHING: the two bodies' radii and the margin, measured between
		// centres in the plane.
		if (ApartCm > MineCm + Enemy->GetSimpleCollisionRadius() + ContactMarginCm)
		{
			continue;
		}
		// AHEAD: within the cone of the way the character is asking to go.
		if (ApartCm > KINDA_SMALL_NUMBER
			&& FVector::DotProduct(Toward / ApartCm, Along) < Cone)
		{
			continue;
		}
		if (ApartCm < NearestCm)
		{
			NearestCm = ApartCm;
			Nearest = Enemy;
		}
	}
	return Nearest;
}

AActor* UCataclysmShoulderThrough::Step(AActor* Character, const FVector& Direction)
{
	UCataclysmAbilitySystemComponent* Mine = Cast<UCataclysmAbilitySystemComponent>(
		UCataclysmTargeting::AbilitySystemOf(Character));
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Mine || !World
		|| Mine->StatForSkill(FName(Stat), FGameplayTagContainer(), 0.0f) <= 0.0f)
	{
		return nullptr;
	}

	AActor* Enemy = EnemyMovedInto(Character, Direction);
	if (!Enemy || !Mine->MayShoulderThrough(Enemy))
	{
		return nullptr;
	}
	Mine->NoteShoulderedThrough(Enemy, World->GetTimeSeconds() + SecondsPerEnemy);

	// ASIDE: square to the way the character is going, toward the side the
	// enemy already stands on, and to the right when it is dead ahead.
	FVector Along = Direction;
	Along.Z = 0.0f;
	Along.Normalize();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Along);
	const float Side = FVector::DotProduct(
		Enemy->GetActorLocation() - Character->GetActorLocation(), Right);
	UCataclysmSkillEffects::ApplyPushAside(Character, Enemy,
										   Side < -1.0f ? -Right : Right, PushCm);

	// AND ONE ORDINARY MELEE HIT OF THE WEAPON'S DAMAGE. Not a skill use, so no
	// skill tags: a melee delivery is what makes it melee.
	FCataclysmHitDelivery Melee;
	Melee.bIsMelee = true;
	UCataclysmSkillEffects::ApplyHit(Character, Enemy, 100.0f, FGameplayTagContainer(), Melee);
	return Enemy;
}
