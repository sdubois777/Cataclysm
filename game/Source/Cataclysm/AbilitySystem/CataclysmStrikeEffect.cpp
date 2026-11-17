// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmStrikeEffect.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmElementVisuals.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

const TCHAR* UCataclysmStrikeEffect::SystemAssetPath =
	TEXT("/Game/Effects/Systems/Skills/NS_Strike_Arc.NS_Strike_Arc");

const FName UCataclysmStrikeEffect::ElementColourParameter(
	CataclysmEffectParameterNames::ElementColour);
const FName UCataclysmStrikeEffect::ElementColourDarkParameter(
	CataclysmEffectParameterNames::ElementColourDark);
const FName UCataclysmStrikeEffect::ScaleParameter(
	CataclysmEffectParameterNames::Scale);

int32 UCataclysmStrikeEffect::TimesAsked = 0;

namespace
{
	/**
	 * Kept alive deliberately, for the same reason the impact and projectile
	 * systems are: it is loaded on the first swing of a session and every swing
	 * afterwards reuses it, and loading a Niagara system per swing would be a
	 * stall in the middle of a fight. A repeating strike such as Pyroclasm swings
	 * every 0.2 seconds, so this is the shape most exposed to that cost. Nothing
	 * else references the asset, because the effect is spawned from code rather
	 * than placed in a level, so without AddToRoot garbage collection would be
	 * free to take it back.
	 *
	 * NAMED FOR THIS FILE even though it sits in an anonymous namespace, because
	 * a unity build merges those into one namespace too and two files declaring
	 * `CachedSystem` would collide.
	 */
	TWeakObjectPtr<UNiagaraSystem> CataclysmCachedStrikeArcSystem;

	UNiagaraSystem* CataclysmLoadStrikeArcSystem()
	{
		if (CataclysmCachedStrikeArcSystem.IsValid())
		{
			return CataclysmCachedStrikeArcSystem.Get();
		}

		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(
			nullptr, UCataclysmStrikeEffect::SystemAssetPath);
		if (System)
		{
			System->AddToRoot();
			CataclysmCachedStrikeArcSystem = System;
		}
		return System;
	}
}

float UCataclysmStrikeEffect::ScaleFor(float RadiusCm)
{
	// Centimetres to metres, because User.Scale is documented across every
	// system as "the ability's radius in centimetres divided by 100".
	return FMath::Clamp(RadiusCm / 100.0f, MinimumScale, MaximumScale);
}

FName UCataclysmStrikeEffect::DamageTypeFor(const AActor* Caster,
											const FGameplayTag& SkillElement)
{
	// THE CASTER FIRST. An enemy's own damage type is what its hits are resisted
	// as, and an effect that disagreed with it would mislead in the one case
	// where the colour tells the player something they can act on.
	const FName FromCaster = UCataclysmSkillEffects::DamageTypeOf(Caster);
	if (!FromCaster.IsNone())
	{
		return FromCaster;
	}

	// THEN THE SKILL, which is what makes a player's swing draw in its own
	// colour rather than white. See the declaration for why the two questions
	// are different and why a player's damage carries no type.
	if (!SkillElement.IsValid())
	{
		return NAME_None;
	}

	// DECODED THROUGH THE ONE DECODER RATHER THAN BY TAKING THE LEAF HERE.
	// UCataclysmDamageCalculation::ElementTagFor and DamageTypeFromTags are the
	// only encoding and decoding of a damage type in the project and its own
	// comment says why they live beside each other: if the two ever disagreed
	// the type would vanish silently. A third place that split a tag on a dot
	// would be exactly that disagreement waiting to happen.
	FGameplayTagContainer JustThisOne;
	JustThisOne.AddTag(SkillElement);
	return UCataclysmDamageCalculation::DamageTypeFromTags(JustThisOne);
}

FRotator UCataclysmStrikeEffect::FacingFor(const FVector& Direction)
{
	// FLATTENED BEFORE IT IS TURNED INTO A ROTATION. A swing sweeps across the
	// ground; a cursor above or below the caster would otherwise tilt the arc
	// onto its edge, where a flat mesh is nearly invisible from a camera looking
	// down at 60 degrees.
	FVector Flat = Direction;
	Flat.Z = 0.0f;

	// A ZERO DIRECTION IS A REAL CASE AND NOT A FAULT. AimDirection returns zero
	// when a player controller has no cursor position yet, which is every
	// automation test and the first frame of play. FVector::Rotation on a zero
	// vector produces a rotation nothing sensible can be said about, so this
	// answers with no rotation at all: the arc points along world +X, which is
	// wrong-looking but finite and not a NaN travelling into a transform.
	if (Flat.IsNearlyZero())
	{
		return FRotator::ZeroRotator;
	}

	return Flat.GetSafeNormal().Rotation();
}

UNiagaraComponent* UCataclysmStrikeEffect::PlayAt(
	const UObject* WorldContextObject, const FVector& Location,
	const FVector& Direction, FName DamageType, float RadiusCm)
{
	// COUNTED FIRST, BEFORE ANYTHING CAN REFUSE. See the declaration: this is
	// the only thing an automation test can observe about this function, and it
	// has to record "a swing asked for an arc" rather than "an arc appeared".
	++TimesAsked;

	if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(
		WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	UNiagaraSystem* System = CataclysmLoadStrikeArcSystem();
	if (!System)
	{
		return nullptr;
	}

	// SPAWNED INACTIVE AND ACTIVATED AFTERWARDS, AND THE ORDER IS THE POINT.
	// Niagara runs an emitter's spawn script when the system activates, so a
	// system that auto-activates has already coloured its particle from the
	// parameter defaults by the time a setter runs. The arc is a single particle
	// spawned in that first burst and it lives 0.28 seconds, so it would keep the
	// authored white for its whole life rather than for one frame. The projectile
	// body effect carries the same note and it was learned there.
	UNiagaraComponent* Component =
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, System, Location, FacingFor(Direction), FVector::OneVector,
			/*bAutoDestroy=*/true, /*bAutoActivate=*/false);
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
	// No else. A swing with no damage type leaves the system's authored defaults
	// in place, which are white and black, and white is the value chosen
	// precisely because no designed row uses it. Every player strike takes this
	// path today -- issue #803, where a player skill's DamageType never reaches
	// its effects.

	Component->SetVariableFloat(ScaleParameter, ScaleFor(RadiusCm));

	// NOTHING SETS Intensity, Duration, ImpactNormal OR TargetPosition, and the
	// system exposes all four anyway. docs/Niagara_Conventions.md section 2
	// requires the whole standard block on every system whether or not it uses
	// every entry, so that a skill row can set them without knowing which system
	// it spawned. There is nothing to set them from yet: no skill carries a
	// loudness figure, and a swing has struck no surface.

	Component->Activate(/*bReset=*/true);
	return Component;
}
