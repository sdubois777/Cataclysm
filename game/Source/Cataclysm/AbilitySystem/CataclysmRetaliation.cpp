// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmRetaliation.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmLeech.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

const TCHAR* UCataclysmRetaliation::AmountStat = TEXT("retaliation");
const TCHAR* UCataclysmRetaliation::RadiusMetresStat =
	TEXT("retaliation_radius_metres");
const TCHAR* UCataclysmRetaliation::LeechesStat = TEXT("retaliation_leeches");

namespace
{
	/**
	 * How much health one actor has right now, or zero if it can hold none.
	 *
	 * NAMED FOR WHAT IT IS FOR RATHER THAN FOR WHAT IT DOES, because the Unreal
	 * unity build concatenates these translation units: a private helper called
	 * `HealthOf` in two files compiles until both are clean and then collides.
	 * That has cost a build cycle here before.
	 */
	float HealthOfRetaliationTarget(const AActor* Actor)
	{
		const UAbilitySystemComponent* AbilitySystem =
			UCataclysmTargeting::AbilitySystemOf(Actor);
		const FGameplayAttribute Health =
			UCataclysmVitalAttributeSet::GetHealthAttribute();
		if (!AbilitySystem || !AbilitySystem->HasAttributeSetForAttribute(Health))
		{
			return 0.0f;
		}
		return AbilitySystem->GetNumericAttribute(Health);
	}

	/**
	 * One of this character's stats, judged against its state right now.
	 *
	 * THE ATTRIBUTE IS THE FALLBACK, so an ability system this project did not
	 * make, and one that has not had a character stat line applied to it yet,
	 * both answer with exactly what the attribute holds.
	 */
	float StatOfRetaliator(const UAbilitySystemComponent* Defender,
						   const TCHAR* Stat, const FGameplayAttribute& Attribute)
	{
		if (!Defender || !Defender->HasAttributeSetForAttribute(Attribute))
		{
			return 0.0f;
		}

		const float Held = Defender->GetNumericAttribute(Attribute);
		const UCataclysmAbilitySystemComponent* Asking =
			Cast<const UCataclysmAbilitySystemComponent>(Defender);

		// NO SKILL TAGS. See the header: retaliation is not a skill and carries
		// none of its own.
		return Asking ? Asking->StatForSkill(FName(Stat), FGameplayTagContainer(),
											 Held)
					  : Held;
	}
}

float UCataclysmRetaliation::AmountFor(const UAbilitySystemComponent* Defender,
									   float DamageTaken)
{
	if (DamageTaken <= 0.0f)
	{
		// A BLOW WORTH NOTHING SENDS NOTHING BACK. The caller only reaches
		// here when something got through, so this is a guard rather than an
		// ordinary case, and it keeps the share below from being taken of a
		// negative number.
		return 0.0f;
	}

	const float Percent = StatOfRetaliator(
		Defender, AmountStat,
		UCataclysmCombatAttributeSet::GetRetaliationAttribute());
	if (Percent <= 0.0f)
	{
		return 0.0f;
	}

	// AND IT COUNTS AS DAMAGE. See the header: the share is what the stat
	// says, and then every increase the character carries applies to it the
	// way it applies to an ordinary attack. NO SKILL TAGS, for the reason
	// StatOfRetaliator gives: retaliation is not a skill and carries none.
	return UCataclysmSkillEffects::ModifiedDamage(
		Defender, DamageTaken * Percent / 100.0f, FGameplayTagContainer());
}

float UCataclysmRetaliation::RadiusMetresFor(
	const UAbilitySystemComponent* Defender)
{
	// FLOORED AT ZERO. Nothing states a negative radius, and a negative one
	// would be handed to a sphere search that cannot use it. `PreAttributeChange`
	// on the combat set already floors the attribute; this floors what a
	// conditional row could otherwise produce.
	return FMath::Max(
		0.0f,
		StatOfRetaliator(
			Defender, RadiusMetresStat,
			UCataclysmCombatAttributeSet::GetRetaliationRadiusMetresAttribute()));
}

bool UCataclysmRetaliation::LeechesFor(const UAbilitySystemComponent* Defender)
{
	// ANY VALUE ABOVE ZERO IS YES, which is how every other flag stat in the
	// project is read. Two sources would sum to 2 and mean the same thing.
	return StatOfRetaliator(
			   Defender, LeechesStat,
			   UCataclysmCombatAttributeSet::GetRetaliationLeechesAttribute())
		> 0.0f;
}

TArray<AActor*> UCataclysmRetaliation::TargetsOf(
	const UAbilitySystemComponent* Defender, AActor* Attacker)
{
	TArray<AActor*> Targets;

	// FIRST AND ALWAYS, whether or not it is inside the radius. See the header.
	if (Attacker)
	{
		Targets.Add(Attacker);
	}

	const float RadiusMetres = RadiusMetresFor(Defender);
	if (RadiusMetres <= 0.0f)
	{
		// WHICH IS EVERY CHARACTER IN THE GAME WITHOUT REPRISAL WAVE, so the
		// search is skipped rather than run with a radius of nothing.
		return Targets;
	}

	// THE AVATAR AND NOT THE OWNER. `GetOwningActor` on an attribute set answers
	// with the ability system's owner, and for the player that is the player
	// state, which is not in the world and has no location to search around.
	// The same distinction is why the hit effect and the death path both reach
	// for the avatar; issues #562 and #565.
	AActor* Avatar = Defender ? Defender->GetAvatarActor() : nullptr;
	if (!Avatar)
	{
		return Targets;
	}

	// AND THE AVATAR IS ALSO WHOSE SIDE DECIDES WHO IS AN ENEMY, which is what
	// keeps the wave off the character's own summons and off anything else on
	// its side. `FindEnemiesInSphere` also refuses scenery, refuses the dead,
	// and never returns the actor passed to it.
	const TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
		Avatar->GetWorld(), Avatar, Avatar->GetActorLocation(),
		RadiusMetres * CentimetresPerMetre);

	for (AActor* Found : Nearby)
	{
		// NOBODY IS STRUCK TWICE. The attacker is normally standing inside the
		// sphere as well, so without this it would take two payments.
		if (Found != Attacker)
		{
			Targets.Add(Found);
		}
	}

	return Targets;
}

float UCataclysmRetaliation::Pay(UAbilitySystemComponent* Defender,
								 AActor* Instigator, AActor* Attacker,
								 float DamageTaken)
{
	const float Amount = AmountFor(Defender, DamageTaken);
	if (Amount <= 0.0f)
	{
		// A CHARACTER WITH NONE OF THE STAT SENDS NOTHING BACK. Three things
		// grant it: the Masochist's class line, the Spaulders shoulder base as
		// an implicit, and the `Stat_Flat_retaliation` suffix, which fits seven
		// slots and can therefore reach any class. Enemies have none, so this is
		// the ordinary case rather than a fault.
		return 0.0f;
	}

	// FOUR THINGS THIS BLOW MAY NOT DO. See the header for why each one, and
	// note that the first is what stops two retaliating characters reflecting
	// at one another without end. It became necessary the moment retaliation
	// started going through the damage formula rather than around it.
	FCataclysmHitDelivery Delivery;
	Delivery.bCannotBeRetaliatedAgainst = true;
	Delivery.bCannotCriticallyStrike = true;
	Delivery.bCarriesNoWeaponSubType = true;
	Delivery.bCannotLeech = true;

	float TakenAltogether = 0.0f;
	for (AActor* Target : TargetsOf(Defender, Attacker))
	{
		// MEASURED RATHER THAN ASSUMED, and that is the design's overkill rule.
		// What a target with 25 health left actually loses to a payment of 400
		// is 25, and the target's own armour and resistance now take a share
		// before that -- so what was sent and what landed differ twice over.
		const float Before = HealthOfRetaliationTarget(Target);
		UCataclysmSkillEffects::ApplyDirectDamage(Instigator, Target, Amount,
												  Delivery);
		TakenAltogether +=
			FMath::Max(0.0f, Before - HealthOfRetaliationTarget(Target));
	}

	// AND THE DEFENDER LEECHES FROM IT, IF IT HAS BOUGHT THAT. Issue #1048, the
	// Masochist's Feeding Wound. Nothing happens for anybody else, because the
	// flag is zero for every character without that capstone option.
	if (TakenAltogether > 0.0f && LeechesFor(Defender))
	{
		UCataclysmLeech::NoteRetaliation(Defender, TakenAltogether);
	}

	return TakenAltogether;
}
