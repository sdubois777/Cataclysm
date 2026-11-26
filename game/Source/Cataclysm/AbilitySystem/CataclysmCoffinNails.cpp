// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmCoffinNails.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

int32 UCataclysmCoffinNails::DriveInto(AActor* Target, AActor* Driver, float PerNail, float Ceiling, float HeldSeconds)
{
	if (!IsValid(Target) || !IsValid(Driver) || PerNail <= 0.0f || HeldSeconds <= 0.0f)
	{
		return 0;
	}
	const UWorld* World = Target->GetWorld();
	if (!World)
	{
		return 0;
	}

	// ONE PER CREATURE, REFRESHED RATHER THAN ADDED TO, as `UCataclysmCurseSpread::MarkOn` keeps its mark.
	UCataclysmCoffinNails* Held = Target->FindComponentByClass<UCataclysmCoffinNails>();
	if (!Held)
	{
		Held = NewObject<UCataclysmCoffinNails>(Target);
		if (!Held)
		{
			return 0;
		}
		Held->RegisterComponent();
	}

	// A NAIL AFTER THE NAILS LAPSED, OR ANOTHER NAILER'S, IS THE FIRST AGAIN: "they stay until 4 seconds pass without a
	// new one", and one nailer's nails at a time (see the header).
	const double Now = World->GetTimeSeconds();
	if (Held->Nailer.Get() != Driver || Held->HaveLapsed(Now))
	{
		Held->Nails = 0;
	}
	Held->Nailer = Driver;
	Held->PointsPerNail = PerNail;
	Held->CeilingPoints = Ceiling;
	Held->HoldSeconds = HeldSeconds;
	Held->LastNailAt = Now;

	// "UP TO 4 NAILS": as many as the ceiling allows a nail's points, so a fifth nail renews the four and counts as none
	// more. At least one, so a ceiling below one nail still lets that nail be driven, held to the ceiling when read.
	const int32 MostThatCount = Ceiling > 0.0f
		? FMath::Max(1, FMath::FloorToInt(Ceiling / PerNail + UE_KINDA_SMALL_NUMBER))
		: MAX_int32;
	Held->Nails = FMath::Min(Held->Nails + 1, MostThatCount);
	return Held->Nails;
}

int32 UCataclysmCoffinNails::NailsIn(const AActor* Target, const AActor* Driver)
{
	if (!Target || !Driver)
	{
		return 0;
	}
	const UCataclysmCoffinNails* Held = Target->FindComponentByClass<UCataclysmCoffinNails>();
	const UWorld* World = Target->GetWorld();
	if (!Held || !World || Held->Nailer.Get() != Driver || Held->HaveLapsed(World->GetTimeSeconds()))
	{
		// The ordinary case: a creature nobody has nailed, another's nails, or nails four seconds old.
		return 0;
	}
	return Held->Nails;
}

float UCataclysmCoffinNails::CritChanceAgainst(const AActor* Target, const AActor* Striker)
{
	const int32 Count = NailsIn(Target, Striker);
	if (Count <= 0)
	{
		return 0.0f;
	}
	const UCataclysmCoffinNails* Held = Target->FindComponentByClass<UCataclysmCoffinNails>();
	const float FromEach = static_cast<float>(Count) * Held->PointsPerNail;
	return Held->CeilingPoints > 0.0f ? FMath::Min(FromEach, Held->CeilingPoints) : FromEach;
}

bool UCataclysmCoffinNails::HaveLapsed(double Now) const
{
	// GONE ONCE THE HOLD HAS PASSED SINCE THE LAST NAIL, so they stay for its 4 seconds and not a moment more.
	return HoldSeconds <= 0.0f || Now - LastNailAt >= static_cast<double>(HoldSeconds);
}
