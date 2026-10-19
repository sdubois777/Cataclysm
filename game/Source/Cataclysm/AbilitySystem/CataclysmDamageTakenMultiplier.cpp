// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmDamageTakenMultiplier.h"

#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	/** One character's hold: what its blows are multiplied by, until when, in its world's seconds. */
	struct FDamageTakenHold
	{
		float Multiplier = 1.0f;
		float UntilSeconds = 0.0f;
	};

	/** Every hold, keyed weakly by the held character's ability system. Gone and ended holds are dropped at the next. */
	TMap<TWeakObjectPtr<UAbilitySystemComponent>, FDamageTakenHold>& DamageTakenHolds()
	{
		static TMap<TWeakObjectPtr<UAbilitySystemComponent>, FDamageTakenHold> Held;
		return Held;
	}

	float DamageTakenWorldSecondsOf(const UAbilitySystemComponent* System)
	{
		const UWorld* World = System ? System->GetWorld() : nullptr;
		return World ? World->GetTimeSeconds() : 0.0f;
	}

	const FDamageTakenHold* DamageTakenHoldOn(const UAbilitySystemComponent* System)
	{
		return System ? DamageTakenHolds().Find(
							TWeakObjectPtr<UAbilitySystemComponent>(const_cast<UAbilitySystemComponent*>(System)))
					  : nullptr;
	}
}

bool UCataclysmDamageTakenMultiplier::Hold(AActor* Target, float Multiplier, float Seconds)
{
	UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Target);
	if (!System || Multiplier <= 1.0f || Seconds <= 0.0f)
	{
		return false;
	}
	const float Now = DamageTakenWorldSecondsOf(System);

	TMap<TWeakObjectPtr<UAbilitySystemComponent>, FDamageTakenHold>& Holds = DamageTakenHolds();
	for (auto It = Holds.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value().UntilSeconds <= DamageTakenWorldSecondsOf(It.Key().Get()))
		{
			It.RemoveCurrent();
		}
	}

	// A HOLD WHILE ANOTHER RUNS KEEPS THE LARGER MULTIPLIER AND THE LATER END; the two do not multiply.
	FDamageTakenHold& Made = Holds.FindOrAdd(TWeakObjectPtr<UAbilitySystemComponent>(System));
	Made.Multiplier = FMath::Max(Made.Multiplier, Multiplier);
	Made.UntilSeconds = FMath::Max(Made.UntilSeconds, Now + Seconds);
	return true;
}

float UCataclysmDamageTakenMultiplier::On(const UAbilitySystemComponent* Defender)
{
	const FDamageTakenHold* Running = DamageTakenHoldOn(Defender);
	return Running && DamageTakenWorldSecondsOf(Defender) < Running->UntilSeconds ? Running->Multiplier : 1.0f;
}

float UCataclysmDamageTakenMultiplier::SecondsLeftOn(const AActor* Target)
{
	const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Target);
	const FDamageTakenHold* Running = DamageTakenHoldOn(System);
	return Running ? FMath::Max(0.0f, Running->UntilSeconds - DamageTakenWorldSecondsOf(System)) : 0.0f;
}
