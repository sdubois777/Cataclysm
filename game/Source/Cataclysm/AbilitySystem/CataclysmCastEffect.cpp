// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmCastEffect.h"
#include "AbilitySystem/CataclysmDamageCalculation.h"
#include "AbilitySystem/CataclysmElementVisuals.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

const TCHAR* UCataclysmCastEffect::SystemAssetPath =
	TEXT("/Game/Effects/Systems/Skills/NS_Cast_Windup.NS_Cast_Windup");

const FName UCataclysmCastEffect::ElementColourParameter(
	CataclysmEffectParameterNames::ElementColour);
const FName UCataclysmCastEffect::ElementColourDarkParameter(
	CataclysmEffectParameterNames::ElementColourDark);
const FName UCataclysmCastEffect::ScaleParameter(
	CataclysmEffectParameterNames::Scale);

int32 UCataclysmCastEffect::TimesAsked = 0;
FName UCataclysmCastEffect::LastDamageTypeAsked = NAME_None;

namespace
{
	/**
	 * Kept alive deliberately, for the same reason the other four effect
	 * systems are: it is loaded on the first skill of a session and every skill
	 * afterwards reuses it. This one is the most exposed of the five, because it
	 * plays on EVERY skill rather than on one shape, so a load per use would be
	 * a stall every time a button is pressed. Nothing else references the asset
	 * -- it is spawned from code rather than placed in a level -- so without
	 * AddToRoot garbage collection would be free to take it back.
	 *
	 * NAMED FOR THIS FILE even though it sits in an anonymous namespace, because
	 * a unity build merges those into one namespace too and two files declaring
	 * `CachedSystem` would collide.
	 */
	TWeakObjectPtr<UNiagaraSystem> CataclysmCachedCastWindupSystem;

	UNiagaraSystem* CataclysmLoadCastWindupSystem()
	{
		if (CataclysmCachedCastWindupSystem.IsValid())
		{
			return CataclysmCachedCastWindupSystem.Get();
		}

		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(
			nullptr, UCataclysmCastEffect::SystemAssetPath);
		if (System)
		{
			System->AddToRoot();
			CataclysmCachedCastWindupSystem = System;
		}
		return System;
	}
}

float UCataclysmCastEffect::ScaleFor(float RadiusCm)
{
	// A radius of zero or less is a self buff, which states none. It takes the
	// minimum rather than nothing, because a buff going off should be visible.
	const float Metres = RadiusCm / 100.0f;
	return FMath::Clamp(Metres, MinimumScale, MaximumScale);
}

FName UCataclysmCastEffect::DamageTypeFor(const AActor* Caster,
										  const FGameplayTag& SkillElement)
{
	// THE CASTER FIRST. Nothing an enemy does reaches this shape today, because
	// skill templates are granted only through the player's weapon slots, but
	// the order is the same one the strike arc and the projectile body use and a
	// third variation of it is how the three would come to disagree.
	const FName FromCaster = UCataclysmSkillEffects::DamageTypeOf(Caster);
	if (!FromCaster.IsNone())
	{
		return FromCaster;
	}

	if (!SkillElement.IsValid())
	{
		return NAME_None;
	}

	// DECODED THROUGH THE ONE DECODER rather than by splitting the tag here.
	// UCataclysmDamageCalculation::ElementTagFor and DamageTypeFromTags are the
	// only encoding and decoding of a damage type in the project, and a third
	// place that took the leaf off a tag is how they would come to disagree.
	FGameplayTagContainer JustThisOne;
	JustThisOne.AddTag(SkillElement);
	return UCataclysmDamageCalculation::DamageTypeFromTags(JustThisOne);
}

FRotator UCataclysmCastEffect::FacingFor(const FVector& Direction,
										 const AActor* Caster)
{
	// FLATTENED BEFORE IT BECOMES A ROTATION. The camera looks down at 60
	// degrees, and a burst tipped up at the sky presents its edge, which is
	// nearly nothing.
	FVector Flat = Direction;
	Flat.Z = 0.0f;

	if (Flat.IsNearlyZero())
	{
		// A SELF BUFF AIMS AT NOTHING and AimDirection answers zero for it. The
		// caster's own facing is the only direction available, and it is the
		// right one: a buff going off should read as coming out of the caster
		// the way they are already turned.
		return Caster ? FRotator(0.0f, Caster->GetActorRotation().Yaw, 0.0f)
					  : FRotator::ZeroRotator;
	}

	return Flat.Rotation();
}

UNiagaraComponent* UCataclysmCastEffect::PlayFor(const AActor* Caster,
												 const FVector& Direction,
												 FName DamageType,
												 float RadiusCm)
{
	// COUNTED FIRST, BEFORE ANYTHING CAN REFUSE. See the declarations: these two
	// are the only things an automation test can observe about this function,
	// and they have to record "a skill asked for a burst" rather than "a burst
	// appeared".
	++TimesAsked;
	LastDamageTypeAsked = DamageType;

	if (!Caster)
	{
		return nullptr;
	}

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(
		Caster, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	UNiagaraSystem* System = CataclysmLoadCastWindupSystem();
	if (!System)
	{
		return nullptr;
	}

	// SPAWNED INACTIVE AND ACTIVATED AFTERWARDS, AND THE ORDER IS THE POINT.
	// Niagara runs an emitter's spawn script when the system activates, so a
	// system that auto-activates has already coloured its particles from the
	// parameter defaults by the time a setter runs. Every layer here is a
	// one-shot burst spawned in that first frame, so all four would keep the
	// authored white for their whole lives rather than for one frame. The
	// projectile body effect carries the same note and it was learned there.
	UNiagaraComponent* Component =
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, System, Caster->GetActorLocation(),
			FacingFor(Direction, Caster), FVector::OneVector,
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
	// No else. A skill naming no damage type leaves the authored defaults, which
	// are white and black, and white is the value chosen precisely because no
	// designed row uses it. Every row of game/Data/WeaponSkills.csv that carries
	// a shape names one, so nothing in the shipped data takes this path.

	Component->SetVariableFloat(ScaleParameter, ScaleFor(RadiusCm));

	// NOTHING SETS Intensity, Duration, ImpactNormal OR TargetPosition, and the
	// system exposes all four anyway. docs/Niagara_Conventions.md section 2
	// requires the whole standard block on every system whether or not it uses
	// every entry, so a skill row can set them without knowing which system it
	// spawned. There is nothing to set them from yet: no skill carries a
	// loudness figure, and a cast has struck no surface.

	Component->Activate(/*bReset=*/true);
	return Component;
}
