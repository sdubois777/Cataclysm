// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmMovement.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Components/CapsuleComponent.h"
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
