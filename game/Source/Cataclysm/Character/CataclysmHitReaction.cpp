// Copyright Stephen Dubois. All Rights Reserved.

#include "Character/CataclysmHitReaction.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "GameFramework/Actor.h"
#include "GameplayEffect.h"

ECataclysmHitDirection UCataclysmHitReaction::DirectionOf(const FVector& Facing, const FVector& ToSource)
{
	// IN THE FLOOR PLANE. See the header: height is not a direction a flinch can show.
	const FVector Forward(Facing.X, Facing.Y, 0.0f);
	const FVector Toward(ToSource.X, ToSource.Y, 0.0f);

	// THE CREATURE'S RIGHT, which is its forward turned ninety degrees clockwise seen from above: Unreal's yaw turns +X
	// toward +Y, so a creature facing +X has +Y on its right.
	const FVector Right(-Forward.Y, Forward.X, 0.0f);

	const double Ahead = FVector::DotProduct(Toward, Forward);
	const double Beside = FVector::DotProduct(Toward, Right);

	// THE NEAREST QUARTER, and a diagonal goes to front or back. Comparing the two projections is the same as comparing
	// the angle with 45 degrees, without an inverse trigonometric call or a unit-length requirement on either input.
	if (FMath::Abs(Ahead) >= FMath::Abs(Beside))
	{
		// NOTHING TO JUDGE IS FRONT: both projections are zero, and zero is not behind.
		return Ahead >= 0.0 ? ECataclysmHitDirection::Front : ECataclysmHitDirection::Back;
	}
	return Beside > 0.0 ? ECataclysmHitDirection::Right : ECataclysmHitDirection::Left;
}

ECataclysmFlinchRefusal UCataclysmHitReaction::RefusalFor(const FCataclysmFlinchFacts& Facts)
{
	if (!Facts.bHasHitReactions)
	{
		return ECataclysmFlinchRefusal::NoHitReactions;
	}
	if (Facts.bDead)
	{
		return ECataclysmFlinchRefusal::Dead;
	}
	if (Facts.bCannotAct)
	{
		return ECataclysmFlinchRefusal::CannotAct;
	}
	if (Facts.bCharging)
	{
		return ECataclysmFlinchRefusal::Charging;
	}
	if (Facts.bWindingUp)
	{
		return ECataclysmFlinchRefusal::WindingUp;
	}
	if (Facts.bFlinching)
	{
		return ECataclysmFlinchRefusal::AlreadyFlinching;
	}
	if (Facts.bAttacking)
	{
		return ECataclysmFlinchRefusal::Attacking;
	}
	return ECataclysmFlinchRefusal::None;
}

FVector UCataclysmHitReaction::WhereTheBlowCameFrom(const FGameplayEffectSpec& Spec, const AActor* Struck)
{
	// 1. THE STAMPED POINT, read exactly as `UCataclysmSkillEffects::MetresFromWhereItWasStruck` reads it.
	if (Spec.GetSetByCallerMagnitude(FName(UCataclysmSkillEffects::StruckFromSetDataName),
			/*WarnIfNotFound=*/false, 0.0f) > 0.0f)
	{
		return FVector(
			Spec.GetSetByCallerMagnitude(FName(UCataclysmSkillEffects::StruckFromXDataName), false, 0.0f),
			Spec.GetSetByCallerMagnitude(FName(UCataclysmSkillEffects::StruckFromYDataName), false, 0.0f),
			Spec.GetSetByCallerMagnitude(FName(UCataclysmSkillEffects::StruckFromZDataName), false, 0.0f));
	}

	const FGameplayEffectContextHandle& Context = Spec.GetContext();

	// 2. THE MINION THAT SWUNG, when the blow was dealt in its summoner's name.
	if (const AActor* Dealer = Cast<AActor>(Context.GetSourceObject()))
	{
		return Dealer->GetActorLocation();
	}

	// 3. WHOEVER DEALT IT, which in this project is always the instigator as well.
	if (const AActor* Causer = Context.GetEffectCauser())
	{
		return Causer->GetActorLocation();
	}
	if (const AActor* Instigator = Context.GetInstigator())
	{
		return Instigator->GetActorLocation();
	}

	// 4. NOBODY NAMED: the creature's own place, which reads as Front.
	return Struck ? Struck->GetActorLocation() : FVector::ZeroVector;
}
