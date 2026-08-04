// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmGroundZone.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillSlots.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

UCataclysmSkillTemplate::UCataclysmSkillTemplate()
{
	// Left at None on the class default. Which slot a skill occupies is decided
	// by the weapon that granted it, not by which template implements it: the
	// same Projectile class fills the Heavy slot for a Staff and the Special
	// slot for a Greataxe. GiveAbilityInSlot stamps it per grant.
	Slot = ECataclysmAbilitySlot::None;
}

float UCataclysmSkillTemplate::GetSlotDamagePercent() const
{
	const UDataTable* Table = UCataclysmSkillSlots::LoadGeneratedTable();
	const FCataclysmSkillSlotNumbers Numbers =
		UCataclysmSkillSlots::NumbersFor(Table, Slot);
	return Numbers.bFound ? Numbers.DamagePercent : 0.0f;
}

bool UCataclysmSkillTemplate::CommitAndBegin(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	// CommitAbility is what runs ApplyCost and ApplyCooldown. Issue #155 wrote
	// both and nothing called them, because the only ability in the project was
	// the placeholder, which ends immediately and commits nothing.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo,
				   /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return false;
	}

	PayHealthCost();
	return true;
}

AActor* UCataclysmSkillTemplate::Avatar() const
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	return Info ? Info->AvatarActor.Get() : nullptr;
}

FVector UCataclysmSkillTemplate::AimPoint() const
{
	const AActor* Self = Avatar();
	const FVector Fallback = Self ? Self->GetActorLocation() : FVector::ZeroVector;

	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	APlayerController* Controller = Info ? Info->PlayerController.Get() : nullptr;
	if (!Controller)
	{
		// An enemy or a minion using a skill. There is no cursor, so the caster's
		// own position is the only answer available.
		return Fallback;
	}

	FHitResult Hit;
	if (Controller->GetHitResultUnderCursor(ECollisionChannel::ECC_Visibility,
											/*bTraceComplex=*/true, Hit))
	{
		return Hit.Location;
	}

	// The cursor is over the sky or past the edge of the floor. Falling back to
	// the caster is what makes a targeted skill fire at their feet rather than
	// at the world origin, which is where an un-hit trace would otherwise put it.
	return Fallback;
}

FVector UCataclysmSkillTemplate::AimDirection() const
{
	const AActor* Self = Avatar();
	if (!Self)
	{
		return FVector::ForwardVector;
	}

	FVector Direction = AimPoint() - Self->GetActorLocation();
	Direction.Z = 0.0f;

	// Nearly zero means the cursor is on the caster, or there is no cursor at
	// all. Either way there is no aimed direction, so the character's own facing
	// is the only sensible answer -- and it is never the zero vector.
	if (Direction.IsNearlyZero())
	{
		FVector Facing = Self->GetActorForwardVector();
		Facing.Z = 0.0f;
		return Facing.IsNearlyZero() ? FVector::ForwardVector : Facing.GetSafeNormal();
	}

	return Direction.GetSafeNormal();
}

FVector UCataclysmSkillTemplate::AimedPointWithin(float RangeCm) const
{
	const AActor* Self = Avatar();
	if (!Self)
	{
		return FVector::ZeroVector;
	}

	const FVector Origin = Self->GetActorLocation();
	if (RangeCm <= 0.0f)
	{
		return Origin;
	}

	// Clamped to the range, so aiming past a skill's reach fires it as far as it
	// goes rather than refusing. Held at the caster's own height, because a
	// cursor trace lands on the floor and a projectile starting at the floor
	// would pass under everything it should hit.
	const FVector Aim = AimPoint();
	FVector Offset = FVector(Aim.X, Aim.Y, Origin.Z) - Origin;
	if (Offset.IsNearlyZero())
	{
		return Origin + AimDirection() * RangeCm;
	}
	if (Offset.SizeSquared() > RangeCm * RangeCm)
	{
		Offset = Offset.GetSafeNormal() * RangeCm;
	}
	return Origin + Offset;
}

float UCataclysmSkillTemplate::HitTargets(const TArray<AActor*>& Targets,
										  float DamagePercent)
{
	AActor* Self = Avatar();
	if (!Self || Targets.IsEmpty())
	{
		return 0.0f;
	}

	const float Percent = DamagePercent >= 0.0f ? DamagePercent : GetSlotDamagePercent();

	float Total = 0.0f;
	for (AActor* Target : Targets)
	{
		const float Dealt = UCataclysmSkillEffects::ApplyHit(Self, Target, Percent);
		Total += Dealt;

		// The burn is a share of the hit that caused it, so a skill that deals
		// no damage sets nothing alight. That is right for a Support skill,
		// whose slot damage is zero by design, and it is why Subjugate reads
		// "subjugating an enemy that is ALREADY burning" rather than burning it
		// itself.
		if (Params.bBurns && Dealt > 0.0f)
		{
			UCataclysmSkillEffects::ApplyBurn(Self, Target, Dealt);
		}
	}

	return Total;
}

ACataclysmGroundZone* UCataclysmSkillTemplate::LeaveGroundAt(const FVector& Location)
{
	// A patch is a path whose two ends are the same point.
	return LeaveGroundAlong(Location, Location);
}

ACataclysmGroundZone* UCataclysmSkillTemplate::LeaveGroundAlong(
	const FVector& Start, const FVector& End)
{
	if (!Params.LeavesGround())
	{
		return nullptr;
	}

	AActor* Self = Avatar();
	if (!Self)
	{
		return nullptr;
	}

	// A TICK OF BURNING GROUND IS WORTH ONE TICK OF BURN. The design gives the
	// ground no damage of its own -- "leaves a pool of lava for 5 seconds that
	// burns anything standing in it" states a duration and nothing else -- so it
	// deals what burn deals, per second, and the skill's own duration decides how
	// long. That keeps one number in the data instead of two, and it means the
	// ground and the burn move together when either is tuned.
	const FCataclysmStatusEffectNumbers Burn = UCataclysmSkillEffects::BurnNumbers();
	if (!Burn.bUsable)
	{
		return nullptr;
	}

	const UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Self);
	const float WeaponDamage = UCataclysmSkillEffects::WeaponDamageOf(AbilitySystem);
	const float PerTick = WeaponDamage * GetSlotDamagePercent() / 100.0f
						* Burn.PercentOfHit / 100.0f
						/ FMath::Max(1.0f, Burn.DurationSeconds);

	return ACataclysmGroundZone::SpawnAlong(Self, Start, End, Params.GroundRadiusCm,
											Params.GroundDuration, PerTick);
}

void UCataclysmSkillTemplate::PayHealthCost()
{
	if (Params.HealthCostPercent <= 0.0f)
	{
		return;
	}

	UAbilitySystemComponent* AbilitySystem =
		UCataclysmTargeting::AbilitySystemOf(Avatar());
	if (!AbilitySystem)
	{
		return;
	}

	// A PERCENT OF CURRENT HEALTH, NOT OF MAXIMUM, because Blood Pyre says so:
	// "paying 8% of your current health". That is what makes it self-limiting --
	// each cast costs less than the last, so it cannot kill the caster.
	const float Current = AbilitySystem->GetNumericAttribute(
		UCataclysmVitalAttributeSet::GetHealthAttribute());
	const float Cost = Current * Params.HealthCostPercent / 100.0f;
	if (Cost > 0.0f)
	{
		AbilitySystem->ApplyModToAttribute(
			UCataclysmVitalAttributeSet::GetHealthAttribute(),
			EGameplayModOp::Additive, -Cost);
	}
}
