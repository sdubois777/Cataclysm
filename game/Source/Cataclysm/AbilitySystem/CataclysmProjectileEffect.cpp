// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmProjectileEffect.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmElementVisuals.h"
#include "AbilitySystem/CataclysmProjectile.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTeams.h"
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

	// THE FIRER FIRST. GetOwner and not GetInstigator: ACataclysmProjectile::Fire
	// sets FActorSpawnParameters::Owner to whoever fired, and every other part
	// of this class -- the sweep's ignore list, the damage it deals, the burn it
	// applies -- reads the firer back the same way. An enemy's own damage type
	// is what its hits are resisted as, and a bolt that disagreed with it would
	// mislead in the one case where the colour tells the player something they
	// can act on.
	const FName FromFirer =
		UCataclysmSkillEffects::DamageTypeOf(Projectile->GetOwner());
	if (!FromFirer.IsNone())
	{
		return FromFirer;
	}

	// THEN THE SKILL THAT FIRED IT, which is what makes a player's bolt draw in
	// its own colour instead of white. A player's damage carries no type, by a
	// decision about resistances that says nothing about what a bolt looks like;
	// the skill row names a damage type for all 58 shaped rows and nothing was
	// reading it. Issue #803.
	//
	// THE TAGS ARE ALREADY HERE. ACataclysmProjectile::Fire is handed the firing
	// skill's whole tag container and keeps it, because the sweep needs it to
	// decide whether the hit can be evaded. One of those tags is the skill's
	// `Element.*`.
	return UCataclysmDamageCalculation::DamageTypeFromTags(
		Projectile->FiringSkillTags());
}

bool UCataclysmProjectileEffect::MayBeCulled(const AActor* Drawer)
{
	// HOSTILE TO THE PLAYER means not on the players' team. A thrall or an imp the player commands is on that team
	// and keeps the budget, as the player's own skills do.
	return !Drawer || UCataclysmTeams::TeamOf(Drawer) == UCataclysmTeams::IdFor(ECataclysmTeam::Players);
}

FCataclysmProjectileLook UCataclysmProjectileEffect::LookFor(const ACataclysmProjectile* Projectile)
{
	FCataclysmProjectileLook Look;
	if (!Projectile)
	{
		return Look;
	}
	Look.bMayBeCulled = MayBeCulled(Projectile->GetOwner());
	if (!Projectile->bQuickCast)
	{
		Look.Scale = ScaleFor(Projectile->BodyRadiusCm);
		return Look;
	}

	// A QUICK CAST: AT ITS BLAST RADIUS, brighter, and with a denser trail. See the header.
	Look.Scale = ScaleFor(Projectile->RadiusCm);
	Look.ColourMultiplier = QuickCastColourMultiplier;
	Look.IntensityMultiplier = QuickCastIntensityMultiplier;
	return Look;
}

float UCataclysmProjectileEffect::DefaultIntensityOf(const UNiagaraSystem* System)
{
	if (!System)
	{
		return 1.0f;
	}
	// THE EXPOSED PARAMETER STORE HOLDS `User.` NAMES WITH THEIR PREFIX, which is how
	// `Cataclysm.Effects.ProjectileBodyExposesTheStandardParameterBlock` reads them too.
	const FNiagaraVariable Intensity(FNiagaraTypeDefinition::GetFloatDef(),
		FName(*(FString(TEXT("User.")) + CataclysmEffectParameterNames::Intensity)));
	const TOptional<float> Value = System->GetExposedParameters().GetParameterOptionalValue<float>(Intensity);
	return Value.IsSet() && Value.GetValue() > 0.0f ? Value.GetValue() : 1.0f;
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
	// HOW IT IS DRAWN, and whether the effect type may refuse it. Issue #2808: a hostile's projectile is spawned
	// with no pre-cull check and is never culled afterwards. See `MayBeCulled`.
	const FCataclysmProjectileLook Look = LookFor(Projectile);

	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
		System, Projectile->GetRootComponent(), NAME_None, FVector::ZeroVector,
		FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset,
		/*bAutoDestroy=*/true, /*bAutoActivate=*/false, ENCPoolMethod::None,
		/*bPreCullCheck=*/Look.bMayBeCulled);
	if (!Component)
	{
		return nullptr;
	}
	if (!Look.bMayBeCulled)
	{
		Component->SetAllowScalability(false);
	}

	FLinearColor Primary;
	FLinearColor Secondary;
	if (UCataclysmElementVisuals::ColoursFor(DamageTypeFor(Projectile), Primary,
											 Secondary))
	{
		// A QUICK CAST'S HUE IS ITS DAMAGE TYPE'S, brighter. The hue is what tells the player which resistance
		// applies, so it is never replaced; only multiplied. Issue #2808.
		Component->SetVariableLinearColor(ElementColourParameter, Primary * Look.ColourMultiplier);
		Component->SetVariableLinearColor(ElementColourDarkParameter, Secondary);
	}
	// No else. A projectile with no damage type leaves the system's authored
	// defaults in place, which are white and black, and white is the value
	// chosen precisely because no designed row uses it. Every player projectile
	// takes this path today -- issue #803.

	Component->SetVariableFloat(ScaleParameter, Look.Scale);

	// A QUICK CAST'S TRAIL IS DENSER: a multiple of the system's own default, read from the asset. Issue #2808.
	if (Look.IntensityMultiplier != 1.0f)
	{
		Component->SetVariableFloat(FName(CataclysmEffectParameterNames::Intensity),
			DefaultIntensityOf(System) * Look.IntensityMultiplier);
	}

	// NOTHING ELSE SETS Intensity, AND NOTHING SETS Duration, ImpactNormal OR TargetPosition, and the
	// system exposes all four anyway. docs/Niagara_Conventions.md section 2
	// requires the whole standard block on every system whether or not it uses
	// every entry, so that a skill row can set them without knowing which system
	// it spawned. There is nothing to set them from yet: no skill carries a
	// loudness figure, and a projectile in flight has neither a duration nor a
	// surface it has struck.

	Component->Activate(/*bReset=*/true);
	return Component;
}
