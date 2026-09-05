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

	// EIGHT, WHICH IS A JUDGEMENT. The Infernal Brand row states no window at
	// all -- only that it explodes at five stacks. Eight seconds means a
	// creature that keeps hitting reaches the explosion and one the player
	// disengages from loses it, which is what makes the modifier a reason to
	// break off rather than a timer that always runs out.
	case ECataclysmStackKind::InfernalBrand:	return 8.0f;

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

	// FIVE, WHICH THE ROW DOES STATE: "When the brand reaches 5 stacks, it
	// explodes". The cap and the trigger are the same number here, unlike
	// every kind above, where the cap is where counting stops.
	case ECataclysmStackKind::InfernalBrand:	return 5;
	default:									break;
	}

	// The same safe direction as the window above: an unknown kind holds nothing
	// rather than growing without bound.
	return 0;
}

const TCHAR* UCataclysmStacks::CarnageFromDamageTakenStat =
	TEXT("carnage_from_damage_taken");

const TCHAR* UCataclysmStacks::CarnageHasNoMaximumStat =
	TEXT("carnage_has_no_maximum");

bool UCataclysmStacks::CarnageFromDamageTaken(
	const UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return false;
	}

	const FGameplayAttribute Flag = UCataclysmClassResourceAttributeSet
		::GetCarnageFromDamageTakenAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Flag))
	{
		// NO CLASS RESOURCE SET MEANS NO CAPSTONE OPTION, which is every enemy
		// in the game, and every enemy goes through the same damage path.
		return false;
	}

	// ASKED FOR RATHER THAN READ, the standing rule for anything a later node
	// might put a condition on. Carnivore's row carries none today, so the
	// attribute is the right fallback and both routes agree.
	return AbilitySystem->StatForSkill(FName(CarnageFromDamageTakenStat),
									   FGameplayTagContainer(),
									   AbilitySystem->GetNumericAttribute(Flag))
		> 0.0f;
}

int32 UCataclysmStacks::CapForOn(
	const UCataclysmAbilitySystemComponent* AbilitySystem,
	ECataclysmStackKind Kind)
{
	// EVERY KIND BUT CARNAGE, AND EVERY CHARACTER BUT ONE, TAKES THE DESIGN'S
	// NUMBER. Issue #1071. Carnivore is the only thing in the game that lifts a
	// cap at all, and it lifts only Carnage's.
	if (Kind != ECataclysmStackKind::Carnage || !AbilitySystem)
	{
		return CapFor(Kind);
	}

	const FGameplayAttribute Flag = UCataclysmClassResourceAttributeSet
		::GetCarnageHasNoMaximumAttribute();
	if (!AbilitySystem->HasAttributeSetForAttribute(Flag))
	{
		return CapFor(Kind);
	}

	// ASKED FOR RATHER THAN READ, for the reason the flag above gives.
	const bool bNoMaximum =
		AbilitySystem->StatForSkill(FName(CarnageHasNoMaximumStat),
									FGameplayTagContainer(),
									AbilitySystem->GetNumericAttribute(Flag))
		> 0.0f;
	return bNoMaximum ? NoMaximum : CapFor(Kind);
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

bool UCataclysmStacks::NoteInfernalBrand(
	UCataclysmAbilitySystemComponent* AbilitySystem)
{
	if (!AbilitySystem)
	{
		return false;
	}

	const ECataclysmStackKind Kind = ECataclysmStackKind::InfernalBrand;

	AbilitySystem->GrantStack(Kind, WindowSecondsFor(Kind), CapFor(Kind));

	if (Held(AbilitySystem, Kind) < CapFor(Kind))
	{
		return false;
	}

	// SPENT HERE RATHER THAN BY THE CALLER, which is what the header promises
	// and what stops the explosion firing on every hit once the fifth stack
	// has landed. The row says the explosion consumes all stacks.
	//
	// CLEARED BY GRANTING A STACK OF ZERO LENGTH, because that is the one way
	// this mechanism has of forgetting: a window applied when the count is
	// ASKED FOR rather than when it would expire means a count with no window
	// left reads as nothing.
	AbilitySystem->GrantStack(Kind, /*WindowSeconds=*/0.0f, /*Cap=*/0);

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

	// AND A STACK OF CARNAGE TOO, FOR A CHARACTER HOLDING CARNIVORE. Issue
	// #1071: "Every hit you take grants a stack of Carnage." Nothing happens
	// here for anybody else, which is every character in the game.
	//
	// A SECOND KIND FROM ONE EVENT AND NOT A REPLACEMENT FOR THE FIRST. The two
	// stay separate because they are separate: Bloodlust lasts 5 seconds and
	// Carnage 8, and different nodes read each. A character holding Carnivore
	// and Blood Offering earns both from the same blow.
	//
	// THROUGH `CapForOn` AND NOT `CapFor`, which is the option's second clause
	// doing its work: Carnage has no maximum for this character.
	if (CarnageFromDamageTaken(AbilitySystem))
	{
		AbilitySystem->GrantStack(
			ECataclysmStackKind::Carnage,
			WindowSecondsFor(ECataclysmStackKind::Carnage),
			CapForOn(AbilitySystem, ECataclysmStackKind::Carnage));

		UE_LOG(LogCataclysm, Verbose,
			   TEXT("A hit taken granted a Carnage stack, now %d."),
			   Held(AbilitySystem, ECataclysmStackKind::Carnage));
	}

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

	// THROUGH `CapForOn` AND NOT `CapFor`, since issue #1071. Carnivore says
	// Carnage has no maximum, and it says so about the stack rather than about
	// one way of earning it, so a kill-granted stack has to obey the same
	// answer a hit-granted one does.
	AbilitySystem->GrantStack(
		ECataclysmStackKind::Carnage,
		WindowSecondsFor(ECataclysmStackKind::Carnage),
		CapForOn(AbilitySystem, ECataclysmStackKind::Carnage));

	UE_LOG(LogCataclysm, Verbose,
		   TEXT("%s gained a Carnage stack for a kill, now %d."),
		   *Killer->GetName(),
		   Held(AbilitySystem, ECataclysmStackKind::Carnage));
	return true;
}
