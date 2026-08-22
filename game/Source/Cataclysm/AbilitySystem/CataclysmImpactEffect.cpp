// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmImpactEffect.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmElementVisuals.h"
#include "AbilitySystemComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

const TCHAR* UCataclysmImpactEffect::SystemAssetPath =
	TEXT("/Game/Effects/Systems/Impacts/NS_Impact_Point.NS_Impact_Point");

const FName UCataclysmImpactEffect::ElementColourParameter(
	CataclysmEffectParameterNames::ElementColour);
const FName UCataclysmImpactEffect::ElementColourDarkParameter(
	CataclysmEffectParameterNames::ElementColourDark);
const FName UCataclysmImpactEffect::TargetPositionParameter(
	CataclysmEffectParameterNames::TargetPosition);
const FName UCataclysmImpactEffect::ImpactNormalParameter(
	CataclysmEffectParameterNames::ImpactNormal);

namespace
{
	/**
	 * Kept alive deliberately. It is loaded on the first hit of a session and
	 * every hit afterwards reuses it; the Niagara system is 475 KB and loading
	 * it per blow would be a stall in the middle of a fight.
	 *
	 * AddToRoot rather than a plain pointer, because nothing else references the
	 * asset -- the effect is spawned from code, not placed in a level -- so
	 * garbage collection would otherwise be free to take it back and the next
	 * hit would pay the load again.
	 */
	TWeakObjectPtr<UNiagaraSystem> CachedImpactSystem;

	UNiagaraSystem* LoadImpactSystem()
	{
		if (CachedImpactSystem.IsValid())
		{
			return CachedImpactSystem.Get();
		}

		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(
			nullptr, UCataclysmImpactEffect::SystemAssetPath);
		if (System)
		{
			System->AddToRoot();
			CachedImpactSystem = System;
		}
		return System;
	}
}

bool UCataclysmImpactEffect::ShouldDrawFor(const FCataclysmIncomingHit& Hit,
										   const FCataclysmDamageResult& Outcome)
{
	// Nothing arrived. Evaded, or mitigated to nothing.
	if (Outcome.DealtToHealth <= 0.0f && Outcome.AbsorbedByShield <= 0.0f)
	{
		return false;
	}

	// Something arrived, but it was a burn or a poison ticking rather than a
	// blow. Issue #563.
	if (Hit.bIsDamageOverTime)
	{
		return false;
	}

	return true;
}

const AActor* UCataclysmImpactEffect::ActorToDrawOn(
	const UAbilitySystemComponent* AbilitySystem)
{
	// GetAvatarActor and NOT GetOwnerActor, and not the attribute set's own
	// GetOwningActor either, which answers with the owner. Issue #562.
	return AbilitySystem ? AbilitySystem->GetAvatarActor() : nullptr;
}

FVector UCataclysmImpactEffect::ImpactLocationFor(const FHitResult* Landed,
												  const AActor* Struck,
												  FVector& OutNormal)
{
	OutNormal = FVector::UpVector;

	const FVector OnTheActor =
		Struck ? Struck->GetActorLocation() : FVector::ZeroVector;

	// bBlockingHit IS THE WHOLE CHECK, and its absence was issue #562. An
	// effect context can carry a hit result that never hit anything; reading its
	// impact point then gives (0,0,0), which is not "no answer" but a specific
	// wrong answer -- the world origin, in the middle of the level.
	if (!Landed || !Landed->bBlockingHit)
	{
		return OnTheActor;
	}

	if (!Landed->ImpactNormal.IsNearlyZero())
	{
		OutNormal = Landed->ImpactNormal;
	}
	return Landed->ImpactPoint;
}

int32 UCataclysmImpactEffect::TimesAsked = 0;
FName UCataclysmImpactEffect::LastDamageTypeAsked = NAME_None;

UNiagaraComponent* UCataclysmImpactEffect::SpawnAt(
	const UObject* WorldContextObject,
	const FVector& Location,
	const FVector& ImpactNormal,
	FName DamageType)
{
	// RECORDED FIRST, BEFORE ANYTHING CAN REFUSE. See the declarations: these
	// two are the only things an automation test can observe about this
	// function.
	++TimesAsked;
	LastDamageTypeAsked = DamageType;

	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World =
		GEngine ? GEngine->GetWorldFromContextObject(
					  WorldContextObject, EGetWorldErrorMode::ReturnNull)
				: nullptr;
	if (!World)
	{
		return nullptr;
	}

	UNiagaraSystem* System = LoadImpactSystem();
	if (!System)
	{
		return nullptr;
	}

	// SPAWNED INACTIVE AND ACTIVATED AFTERWARDS, AND THE ORDER IS THE POINT.
	// Niagara runs an emitter's spawn script when the system activates, so a
	// system that auto-activates has already coloured its first particles from
	// the parameter defaults by the time a setter runs. The colours would then
	// arrive one frame late -- on a burst that lives half a second, that is
	// visible, and on the single-frame core sprite it would mean the authored
	// default colour is the only one ever drawn.
	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		WorldContextObject, System, Location, ImpactNormal.Rotation(),
		FVector(1.0f), /*bAutoDestroy=*/true, /*bAutoActivate=*/false);
	if (!Component)
	{
		return nullptr;
	}

	FLinearColor Primary;
	FLinearColor Secondary;
	if (UCataclysmElementVisuals::ColoursFor(DamageType, Primary, Secondary))
	{
		Component->SetVariableLinearColor(ElementColourParameter, Primary);
		Component->SetVariableLinearColor(ElementColourDarkParameter, Secondary);
	}
	// No else. A damage type with no row leaves the system's authored defaults
	// in place, which are white and black, and white is the value chosen
	// precisely because no designed row uses it.

	Component->SetVariablePosition(TargetPositionParameter, Location);
	Component->SetVariableVec3(ImpactNormalParameter, ImpactNormal);

	Component->Activate(/*bReset=*/true);
	return Component;
}
