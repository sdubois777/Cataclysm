// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmMovement.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Character/CataclysmCharacterBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

bool UCataclysmMovement::StepOutOfEnemies(AActor* Mover, const FVector& LineStart)
{
	const ACharacter* Body = Cast<ACharacter>(Mover);
	UWorld* World = Mover ? Mover->GetWorld() : nullptr;
	if (!Body || !World || !Body->GetCapsuleComponent())
	{
		return false;
	}
	const float MyRadius = Body->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FVector Ended = Mover->GetActorLocation();
	const FVector Toward = FVector(LineStart.X - Ended.X, LineStart.Y - Ended.Y, 0.0f);
	const float Length = Toward.Size();
	const FVector Back = Length > 0.0f ? Toward / Length : FVector::ZeroVector;
	const auto Overlaps = [World, Mover, MyRadius](const FVector& At)
	{
		for (AActor* Enemy : UCataclysmTargeting::FindEnemiesInSphere(World, Mover, At, MyRadius + 200.0f))
		{
			const ACharacter* Theirs = Cast<ACharacter>(Enemy);
			const float TheirRadius = Theirs && Theirs->GetCapsuleComponent()
				? Theirs->GetCapsuleComponent()->GetScaledCapsuleRadius() : 0.0f;
			if (!UCataclysmSkillEffects::IsDead(Enemy) && FVector::Dist2D(At, Enemy->GetActorLocation()) < MyRadius + TheirRadius)
			{
				return true;
			}
		}
		return false;
	};
	for (float Walked = 0.0f; Walked <= Length + 1.0f; Walked += 10.0f)
	{
		const FVector At = Ended + Back * FMath::Min(Walked, Length);
		if (!Overlaps(At))
		{
			if (Walked > 0.0f)
			{
				Mover->SetActorLocation(At, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
				return true;
			}
			return false;
		}
	}
	return false;
}

void UCataclysmMovement::PlaceBeside(AActor* Mover, const AActor* Creature)
{
	if (!Mover || !Creature)
	{
		return;
	}
	const FVector From = Mover->GetActorLocation();
	const FVector Feet(Creature->GetActorLocation().X, Creature->GetActorLocation().Y, From.Z);
	Mover->SetActorLocation(Feet, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	StepOutOfEnemies(Mover, From);
	NoteRelocatedInstantly(Mover, From);
}

FVector UCataclysmMovement::SpotBehind(const AActor* Mover, const AActor* Creature)
{
	if (!Mover || !Creature)
	{
		return Mover ? Mover->GetActorLocation() : FVector::ZeroVector;
	}
	// OPPOSITE THE CREATURE'S FACING, ALONG THE GROUND. A creature facing straight up or down (no ground facing) is
	// read as facing the mover, so "behind" is its far side from the mover.
	FVector Facing = Creature->GetActorForwardVector();
	Facing.Z = 0.0f;
	if (!Facing.Normalize())
	{
		Facing = FVector(Mover->GetActorLocation().X - Creature->GetActorLocation().X,
						 Mover->GetActorLocation().Y - Creature->GetActorLocation().Y, 0.0f);
		if (!Facing.Normalize())
		{
			Facing = FVector::ForwardVector;
		}
	}
	const float Apart = UCataclysmTargeting::BodyRadiusOf(Mover) + UCataclysmTargeting::BodyRadiusOf(Creature)
		+ BehindGapCm;
	const FVector Behind = Creature->GetActorLocation() - Facing * Apart;
	return FVector(Behind.X, Behind.Y, Mover->GetActorLocation().Z);
}

void UCataclysmMovement::PlaceBehind(AActor* Mover, const AActor* Creature)
{
	UWorld* World = Mover ? Mover->GetWorld() : nullptr;
	if (!World || !Creature)
	{
		return;
	}
	FVector Spot = SpotBehind(Mover, Creature);
	// ROOM FIRST: `FindTeleportSpot` answers true, unchanged, for a spot that is already free, and moves it to the nearest
	// free place otherwise. With no room near it at all the mover is put beside the creature instead, as Thousand Cuts
	// puts it.
	if (!World->FindTeleportSpot(Mover, Spot, Mover->GetActorRotation()))
	{
		PlaceBeside(Mover, Creature);
		return;
	}
	Mover->SetActorLocation(Spot, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	// AND OUT OF ANY OTHER ENEMY STANDING THERE, further behind the creature rather than toward it.
	const FVector FurtherBehind = Spot + (Spot - Creature->GetActorLocation()).GetSafeNormal2D() * 1000.0f;
	StepOutOfEnemies(Mover, FurtherBehind);
	// AND FACING IT, which is what being behind it is for.
	const FVector Toward = Creature->GetActorLocation() - Mover->GetActorLocation();
	if (!Toward.IsNearlyZero())
	{
		Mover->SetActorRotation(FRotator(0.0f, Toward.Rotation().Yaw, 0.0f));
	}
}

void UCataclysmMovement::MoveWithoutSticking(AActor* Actor, const FVector& Delta)
{
	if (!Actor || Delta.IsNearlyZero())
	{
		return;
	}
	const ACharacter* Character = Cast<ACharacter>(Actor);
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Movement && Movement->UpdatedComponent)
	{
		FHitResult Hit;
		Movement->SafeMoveUpdatedComponent(Delta, Actor->GetActorQuat(), /*bSweep=*/true, Hit);
		return;
	}
	Actor->AddActorWorldOffset(Delta, /*bSweep=*/true);
}

void UCataclysmMovement::NoteRelocatedInstantly(AActor* Character, const FVector& From)
{
	ACataclysmCharacterBase* Base = Cast<ACataclysmCharacterBase>(Character);
	UCataclysmAbilitySystemComponent* Cataclysm =
		Base ? Cast<UCataclysmAbilitySystemComponent>(Base->GetAbilitySystemComponent()) : nullptr;
	if (Cataclysm)
	{
		Cataclysm->NoteRelocatedInstantly(Base->GetActorLocation() - From);
	}
}

void UCataclysmMovement::SampleStep(ACataclysmCharacterBase* Character)
{
	if (!Character)
	{
		return;
	}

	// AN ABILITY SYSTEM THIS PROJECT DID NOT MAKE KEEPS NO CLOCKS, so there is
	// nowhere to record what this sample saw and nothing reads it either.
	UCataclysmAbilitySystemComponent* Cataclysm =
		Cast<UCataclysmAbilitySystemComponent>(
			Character->GetAbilitySystemComponent());
	if (!Cataclysm)
	{
		return;
	}

	const FVector Now = Character->GetActorLocation();

	if (!Cataclysm->HasSampledLocation())
	{
		// NOTHING TO COMPARE AGAINST YET. Remember where it is and report standing
		// still, which starts the clock so a character that never moves still
		// reads a real number of seconds.
		Cataclysm->NoteSampledAt(Now);
		Cataclysm->NoteDidNotMove();
		return;
	}

	// FLAT DISTANCE, NOT THROUGH THE AIR. A character falling or stepping off a
	// ledge has not walked anywhere, and a row about moving means across the
	// ground.
	const float Metres =
		FVector::Dist2D(Cataclysm->SampledLocation(), Now) / CentimetresPerMetre;

	Cataclysm->NoteSampledAt(Now);

	if (Metres >= MovedMetresThreshold)
	{
		Cataclysm->NoteMovedMetres(Metres);
	}
	else
	{
		Cataclysm->NoteDidNotMove();
	}
}
