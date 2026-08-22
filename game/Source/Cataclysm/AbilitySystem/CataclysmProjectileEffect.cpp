// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmProjectileEffect.h"
#include "AbilitySystem/CataclysmElementVisuals.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

const TCHAR* UCataclysmProjectileEffect::SystemAssetPath =
	TEXT("/Game/Effects/Systems/Skills/NS_Proj_Body.NS_Proj_Body");

const FName UCataclysmProjectileEffect::ElementColourParameter(
	CataclysmEffectParameterNames::ElementColour);
const FName UCataclysmProjectileEffect::ElementColourDarkParameter(
	CataclysmEffectParameterNames::ElementColourDark);
const FName UCataclysmProjectileEffect::ScaleParameter(
	CataclysmEffectParameterNames::Scale);

namespace
{
	/**
	 * Kept alive deliberately, for the same reason the impact system is: it is
	 * loaded on the first shot of a session and every shot afterwards reuses it,
	 * and loading a Niagara system per shot would be a stall in the middle of a
	 * fight. Nothing else references the asset, because the effect is spawned
	 * from code rather than placed in a level, so without AddToRoot garbage
	 * collection would be free to take it back.
	 */
	TWeakObjectPtr<UNiagaraSystem> CachedProjectileSystem;

	UNiagaraSystem* LoadProjectileSystem()
	{
		if (CachedProjectileSystem.IsValid())
		{
			return CachedProjectileSystem.Get();
		}

		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(
			nullptr, UCataclysmProjectileEffect::SystemAssetPath);
		if (System)
		{
			System->AddToRoot();
			CachedProjectileSystem = System;
		}
		return System;
	}
}

float UCataclysmProjectileEffect::ScaleFor(float BodyRadiusCm)
{
	// Centimetres to metres, because User.Scale is documented across every
	// system as "the ability's radius in centimetres divided by 100".
	return FMath::Clamp(BodyRadiusCm / 100.0f, MinimumScale, MaximumScale);
}

FName UCataclysmProjectileEffect::DamageTypeFor(
	const ACataclysmProjectile* Projectile)
{
	if (!Projectile)
	{
		return NAME_None;
	}

	// GetOwner and not GetInstigator: ACataclysmProjectile::Fire sets
	// FActorSpawnParameters::Owner to whoever fired, and every other part of
	// this class -- the sweep's ignore list, the damage it deals, the burn it
	// applies -- reads the firer back the same way.
	return UCataclysmSkillEffects::DamageTypeOf(Projectile->GetOwner());
}

int32 UCataclysmProjectileEffect::TimesAsked = 0;

UNiagaraComponent* UCataclysmProjectileEffect::AttachTo(
	ACataclysmProjectile* Projectile)
{
	// COUNTED FIRST, BEFORE ANYTHING CAN REFUSE. See the declaration: this is
	// the only thing an automation test can observe about this function.
	++TimesAsked;

	if (!Projectile || !Projectile->GetRootComponent())
	{
		return nullptr;
	}

	if (!Projectile->GetWorld())
	{
		return nullptr;
	}

	UNiagaraSystem* System = LoadProjectileSystem();
	if (!System)
	{
		return nullptr;
	}

	// SPAWNED INACTIVE AND ACTIVATED AFTERWARDS, AND THE ORDER IS THE POINT.
	// Niagara runs an emitter's spawn script when the system activates, so a
	// system that auto-activates has already coloured its first particles from
	// the parameter defaults by the time a setter runs. The head sprite is a
	// single particle spawned in that first burst, so it would keep the authored
	// white for its whole life rather than for one frame.
	//
	// ATTACHED TO THE ROOT AND NOT TO THE PLACEHOLDER MESH. The mesh is scaled
	// to the projectile's body width, and a component attached to it would
	// inherit that scale on top of the size User.Scale already asks for.
	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
		System, Projectile->GetRootComponent(), NAME_None, FVector::ZeroVector,
		FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset,
		/*bAutoDestroy=*/true, /*bAutoActivate=*/false);
	if (!Component)
	{
		return nullptr;
	}

	FLinearColor Primary;
	FLinearColor Secondary;
	if (UCataclysmElementVisuals::ColoursFor(DamageTypeFor(Projectile), Primary,
											 Secondary))
	{
		Component->SetVariableLinearColor(ElementColourParameter, Primary);
		Component->SetVariableLinearColor(ElementColourDarkParameter, Secondary);
	}
	// No else. A projectile with no damage type leaves the system's authored
	// defaults in place, which are white and black, and white is the value
	// chosen precisely because no designed row uses it. Every player projectile
	// takes this path today -- issue #803.

	Component->SetVariableFloat(ScaleParameter, ScaleFor(Projectile->BodyRadiusCm));

	// NOTHING SETS Intensity, Duration, ImpactNormal OR TargetPosition, and the
	// system exposes all four anyway. docs/Niagara_Conventions.md section 2
	// requires the whole standard block on every system whether or not it uses
	// every entry, so that a skill row can set them without knowing which system
	// it spawned. There is nothing to set them from yet: no skill carries a
	// loudness figure, and a projectile in flight has neither a duration nor a
	// surface it has struck.

	Component->Activate(/*bReset=*/true);
	return Component;
}
