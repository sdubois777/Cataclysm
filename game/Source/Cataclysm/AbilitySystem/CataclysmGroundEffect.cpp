// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmGroundEffect.h"
#include "AbilitySystem/CataclysmElementVisuals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

const TCHAR* UCataclysmGroundEffect::SystemAssetPath =
	TEXT("/Game/Effects/Systems/Impacts/NS_Impact_Ground.NS_Impact_Ground");

const FName UCataclysmGroundEffect::ElementColourParameter(
	CataclysmEffectParameterNames::ElementColour);
const FName UCataclysmGroundEffect::ElementColourDarkParameter(
	CataclysmEffectParameterNames::ElementColourDark);
const FName UCataclysmGroundEffect::ScaleParameter(
	CataclysmEffectParameterNames::Scale);
const FName UCataclysmGroundEffect::DurationParameter(
	CataclysmEffectParameterNames::Duration);

int32 UCataclysmGroundEffect::TimesAsked = 0;
int32 UCataclysmGroundEffect::TimesEndAsked = 0;
int32 UCataclysmGroundEffect::LastEndCount = 0;

FVector UCataclysmGroundEffect::LastStart = FVector::ZeroVector;
FVector UCataclysmGroundEffect::LastFarEnd = FVector::ZeroVector;
float UCataclysmGroundEffect::LastRadiusCm = 0.0f;
float UCataclysmGroundEffect::LastDuration = 0.0f;
FName UCataclysmGroundEffect::LastDamageTypeAsked = NAME_None;

namespace
{
	/**
	 * Kept alive deliberately, for the same reason the other effect systems
	 * are: loading a Niagara system while a skill is going off is a stall in the
	 * middle of a fight. Nothing else references the asset, because the effect
	 * is spawned from code rather than placed in a level, so without AddToRoot
	 * garbage collection would be free to take it back.
	 *
	 * NAMED FOR THIS FILE even though it sits in an anonymous namespace, because
	 * a unity build merges those into one namespace too and two files declaring
	 * `CachedSystem` would collide.
	 */
	TWeakObjectPtr<UNiagaraSystem> CataclysmCachedGroundSystem;

	UNiagaraSystem* CataclysmLoadGroundSystem()
	{
		if (CataclysmCachedGroundSystem.IsValid())
		{
			return CataclysmCachedGroundSystem.Get();
		}

		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(
			nullptr, UCataclysmGroundEffect::SystemAssetPath);
		if (System)
		{
			System->AddToRoot();
			CataclysmCachedGroundSystem = System;
		}
		return System;
	}
}

float UCataclysmGroundEffect::ScaleFor(float RadiusCm)
{
	// Centimetres to metres, because User.Scale is documented across every
	// system as "the ability's radius in centimetres divided by 100".
	return FMath::Clamp(RadiusCm / 100.0f, MinimumScale, MaximumScale);
}

int32 UCataclysmGroundEffect::HowManyAlong(float LengthCm, float RadiusCm)
{
	// A ROUND ZONE IS ONE COPY. It is also the case a negative or nonsense
	// length falls into, which is the safe answer rather than a loop that never
	// ends.
	if (!(LengthCm > 0.0f) || !(RadiusCm > 0.0f))
	{
		return 1;
	}

	// COPIES ARE SPREAD FROM ONE END TO THE OTHER, so Count - 1 gaps span the
	// length and each gap must be no wider than a diameter or the drawn patch
	// has holes in it that still deal damage.
	const int32 Gaps = FMath::CeilToInt(LengthCm / (RadiusCm * 2.0f));
	return FMath::Clamp(Gaps + 1, 1, MostCopies);
}

TArray<TWeakObjectPtr<UNiagaraComponent>> UCataclysmGroundEffect::PlayFor(
	const UObject* WorldContextObject, const FVector& Start,
	const FVector& FarEnd, float RadiusCm, float Duration, FName DamageType)
{
	TArray<TWeakObjectPtr<UNiagaraComponent>> Drawn;

	// COUNTED FIRST, BEFORE ANYTHING CAN REFUSE. See the declaration: this and
	// the four values below are the only things an automation test can observe
	// about this function.
	//
	// AND WHAT WAS ASKED FOR, NOT ONLY THAT SOMETHING WAS. Issue #1153: every
	// zone in the game asked for a radius of zero and a far end at the world
	// origin for as long as this counter existed, and the counter went up each
	// time.
	++TimesAsked;
	LastStart = Start;
	LastFarEnd = FarEnd;
	LastRadiusCm = RadiusCm;
	LastDuration = Duration;
	LastDamageTypeAsked = DamageType;

	if (!WorldContextObject)
	{
		return Drawn;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(
		WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return Drawn;
	}

	UNiagaraSystem* System = CataclysmLoadGroundSystem();
	if (!System)
	{
		return Drawn;
	}

	FLinearColor Primary;
	FLinearColor Secondary;
	const bool bTyped =
		UCataclysmElementVisuals::ColoursFor(DamageType, Primary, Secondary);
	// No else. An untyped zone leaves the system's authored defaults in place,
	// which are white and black, and white is the value chosen precisely because
	// no designed row uses it. Every zone a player leaves takes this path today
	// -- issue #803.

	const FVector Along = FarEnd - Start;
	const int32 HowMany = HowManyAlong(static_cast<float>(Along.Size()), RadiusCm);

	for (int32 Which = 0; Which < HowMany; ++Which)
	{
		// EVENLY FROM ONE END TO THE OTHER, both ends included. With one copy
		// the fraction is zero and it lands on Start, which is the centre of a
		// round zone.
		const float Fraction = (HowMany > 1)
			? static_cast<float>(Which) / static_cast<float>(HowMany - 1)
			: 0.0f;
		const FVector Where = Start + Along * Fraction;

		// SPAWNED INACTIVE AND ACTIVATED AFTERWARDS, AND THE ORDER IS THE POINT.
		// Niagara runs an emitter's spawn script when the system activates, so a
		// system that auto-activates has already coloured its first particles
		// from the parameter defaults by the time a setter runs. The ring is a
		// single particle spawned in that first burst, so it would keep the
		// authored white for its whole life rather than for one frame.
		UNiagaraComponent* Component =
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				World, System, Where, FRotator::ZeroRotator, FVector::OneVector,
				/*bAutoDestroy=*/true, /*bAutoActivate=*/false);
		if (!Component)
		{
			continue;
		}

		if (bTyped)
		{
			Component->SetVariableLinearColor(ElementColourParameter, Primary);
			Component->SetVariableLinearColor(ElementColourDarkParameter,
											  Secondary);
		}

		Component->SetVariableFloat(ScaleParameter, ScaleFor(RadiusCm));
		Component->SetVariableFloat(DurationParameter, Duration);

		Component->Activate(/*bReset=*/true);

		// KEPT AS A WEAK POINTER BECAUSE THE CALLER DOES NOT OWN IT. It is
		// outered to the world settings actor and destroys itself when its time
		// is up, so it may be gone before anyone looks again.
		Drawn.Add(Component);
	}

	return Drawn;
}

int32 UCataclysmGroundEffect::EndFor(
	const TArray<TWeakObjectPtr<UNiagaraComponent>>& Drawings)
{
	// COUNTED FIRST, BEFORE ANYTHING CAN REFUSE, for the reason the counter in
	// PlayFor is counted first: no test here can see a Niagara component, so
	// the ask is the only observable part -- and the ask is what was missing.
	++TimesEndAsked;
	LastEndCount = Drawings.Num();

	int32 Ended = 0;
	for (const TWeakObjectPtr<UNiagaraComponent>& Weak : Drawings)
	{
		UNiagaraComponent* Component = Weak.Get();
		if (!IsValid(Component))
		{
			// ALREADY GONE IS THE ORDINARY CASE. A drawing that finished its own
			// life destroyed itself, which is exactly what it is meant to do.
			continue;
		}

		// AT ONCE RATHER THAN LETTING IT FINISH. Deactivate would let the
		// particles already alive play out, which is the burning-out behaviour
		// that is right for a patch ending on its own schedule and wrong here:
		// the caller has been cut short, and on a floor change everything else
		// on that floor went in the same instant.
		Component->DestroyComponent();
		++Ended;
	}

	return Ended;
}
