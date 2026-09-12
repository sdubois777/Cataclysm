// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmMovement.h"

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "Character/CataclysmCharacterBase.h"

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
