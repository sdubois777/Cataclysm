// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmStacks.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystemComponent.h"
#include "Cataclysm.h"

float UCataclysmStacks::WindowSecondsFor(ECataclysmStackKind Kind)
{
	switch (Kind)
	{
	// "Each health cost paid within 3 SECONDS of the last grants a stack."
	case ECataclysmStackKind::SanguineMomentum:	return 3.0f;

	// "Taking damage grants a stack of Bloodlust for 5 SECONDS."
	case ECataclysmStackKind::Bloodlust:		return 5.0f;

	// "Killing an enemy ... grants a stack of Carnage for 8 SECONDS."
	case ECataclysmStackKind::Carnage:			return 8.0f;

	default:									break;
	}

	// A KIND THIS BUILD DOES NOT KNOW HOLDS NOTHING. A window of nothing makes
	// `Held` answer zero for it at every instant, which is the safe direction:
	// the alternative would be a stack that never expires.
	return 0.0f;
}

int32 UCataclysmStacks::CapFor(ECataclysmStackKind Kind)
{
	switch (Kind)
	{
	case ECataclysmStackKind::SanguineMomentum:	return 5;	// "up to 5 stacks"
	case ECataclysmStackKind::Bloodlust:		return 5;	// "up to 5 stacks"
	case ECataclysmStackKind::Carnage:			return 10;	// "up to 10 stacks"
	default:									break;
	}

	// The same safe direction as the window above: an unknown kind holds nothing
	// rather than growing without bound.
	return 0;
}

const TCHAR* UCataclysmStacks::NameOf(ECataclysmStackKind Kind)
{
	switch (Kind)
	{
	case ECataclysmStackKind::SanguineMomentum:	return TEXT("Sanguine Momentum");
	case ECataclysmStackKind::Bloodlust:		return TEXT("Bloodlust");
	case ECataclysmStackKind::Carnage:			return TEXT("Carnage");
	default:									return TEXT("(unknown)");
	}
}

int32 UCataclysmStacks::Held(
	const UCataclysmAbilitySystemComponent* AbilitySystem,
	ECataclysmStackKind Kind)
{
	if (!AbilitySystem)
	{
		return 0;
	}
	return AbilitySystem->StacksHeld(Kind, WindowSecondsFor(Kind));
}

bool UCataclysmStacks::NoteHealthCostPaid(
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return false;
	}

	// "WITHIN 3 SECONDS OF THE LAST" NEEDS THERE TO BE A LAST. A negative
	// reading is "never paid one", which is where every character starts a
	// fight, and the first cost of a fight is not within three seconds of
	// anything. Issue #1002, and it is the half of the sentence that is easiest
	// to drop: a version granting a stack on every payment would give a
	// character one from its opening cast.
	const float Since = AbilitySystem->SecondsSinceHealthCostPaid();
	if (Since < 0.0f
		|| Since > WindowSecondsFor(ECataclysmStackKind::SanguineMomentum))
	{
		return false;
	}

	AbilitySystem->GrantStack(
		ECataclysmStackKind::SanguineMomentum,
		WindowSecondsFor(ECataclysmStackKind::SanguineMomentum),
		CapFor(ECataclysmStackKind::SanguineMomentum));
	return true;
}

bool UCataclysmStacks::NoteDamageTaken(
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return false;
	}

	// NO CONDITION AT ALL BEYOND HAVING BEEN HIT. The caller decides what counts
	// as a hit that landed; this is only the count. Issue #1003.
	AbilitySystem->GrantStack(
		ECataclysmStackKind::Bloodlust,
		WindowSecondsFor(ECataclysmStackKind::Bloodlust),
		CapFor(ECataclysmStackKind::Bloodlust));
	return true;
}

bool UCataclysmStacks::NoteEnemyKilled(AActor* Killer)
{
	if (!Killer)
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Killer));
	if (!AbilitySystem)
	{
		return false;
	}

	const FGameplayAttribute Resource =
		UCataclysmClassResourceAttributeSet::GetClassResourceAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Resource))
	{
		// NO CLASS RESOURCE SET MEANS NO FERVOUR TO BE ABOVE, which is every
		// enemy in the game. An enemy killing something gains nothing here.
		return false;
	}

	// STRICTLY ABOVE 75, BECAUSE THE DESIGN WRITES "above 75 Fervour". Issue
	// #1004. A character sitting exactly on 75 gains nothing, which is the same
	// boundary Grand Tithe draws for a health cost.
	if (AbilitySystem->GetNumericAttribute(Resource)
		<= CarnageClassResourceAbove)
	{
		return false;
	}

	AbilitySystem->GrantStack(
		ECataclysmStackKind::Carnage,
		WindowSecondsFor(ECataclysmStackKind::Carnage),
		CapFor(ECataclysmStackKind::Carnage));

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s gained a Carnage stack for a kill, now %d."),
		   *Killer->GetName(),
		   Held(AbilitySystem, ECataclysmStackKind::Carnage));
	return true;
}
