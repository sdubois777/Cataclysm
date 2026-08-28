// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmNova.h"
#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

const TCHAR* UCataclysmNova::DamageStat = TEXT("nova_damage_of_missing_health");

float UCataclysmNova::AmountFrom(float SharePercent, float Health,
								 float MaxHealth)
{
	if (SharePercent <= 0.0f || MaxHealth <= 0.0f)
	{
		// NO STAT, OR NO MAXIMUM TO MEASURE MISSING HEALTH AGAINST. The second
		// is an attribute set that has not been written yet, which reports zero
		// and would otherwise make a full character look infinitely hurt.
		return 0.0f;
	}

	// FLOORED AT NOTHING MISSING. Health above the maximum is reachable for an
	// instant while an attribute set is being filled in, and a negative amount
	// would be a nova that healed what it struck.
	const float Missing = FMath::Max(0.0f, MaxHealth - Health);

	return Missing * SharePercent / 100.0f;
}

float UCataclysmNova::Step(AActor* Character)
{
	// NOTHING FOR THE DEAD, for the reason `UCataclysmLeech::PayOutStep` gives:
	// a creature is destroyed on the step after it dies, so there is a real
	// window in which a corpse is still standing there with an ability system.
	if (UCataclysmSkillEffects::IsDead(Character))
	{
		return 0.0f;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		Cast<UCataclysmAbilitySystemComponent>(
			UCataclysmTargeting::AbilitySystemOf(Character));
	if (!AbilitySystem)
	{
		// An ability system this project did not make records no interval. Every
		// enemy in the game goes through here on its own step.
		return 0.0f;
	}

	const UCataclysmVitalAttributeSet* Vitals =
		AbilitySystem->GetSet<UCataclysmVitalAttributeSet>();
	if (!Vitals)
	{
		return 0.0f;
	}

	// ASKED BEFORE THE CLOCK IS. A character with no point in the node answers
	// zero here and never touches the timestamp, which is every character in the
	// game; asking the other way round would run the stat pipeline several times
	// a second for everybody.
	//
	// NO SKILL TAGS. Nothing is being cast: this is damage arriving because time
	// passed and health is low, so there is no skill whose tags could scope it.
	//
	// A FALLBACK OF ZERO RATHER THAN OF THE ATTRIBUTE. The node's row carries a
	// health condition, so the attribute is zero for a character holding it and
	// passing the attribute would say nothing extra. Zero is also the right
	// answer for an ability system that has never had a character stat line
	// applied to it.
	const float SharePercent = AbilitySystem->StatForSkill(
		FName(DamageStat), FGameplayTagContainer(), 0.0f);

	const float Amount = AmountFrom(SharePercent, Vitals->GetHealth(),
									Vitals->GetMaxHealth());
	if (Amount <= 0.0f)
	{
		// EVERY CHARACTER WITHOUT THE NODE, every character holding it that is
		// not hurt enough for its condition, and one at exactly full health.
		return 0.0f;
	}

	if (!AbilitySystem->MayReleaseNova())
	{
		// RELEASED ONE LESS THAN FIVE SECONDS AGO. This step runs several times
		// a second, so this is the usual answer for a character standing at low
		// health with the node.
		return 0.0f;
	}

	// THE CHARACTER'S OWN LOCATION AND ITS OWN SIDE. `FindEnemiesInSphere`
	// decides who counts as an enemy from the actor passed to it, refuses
	// scenery and the dead, and never returns that actor itself.
	//
	// THE AVATAR IS ALREADY IN HAND HERE, unlike at the retaliation site, because
	// the caller is `ACataclysmCharacterBase::RegenerationStep` and that IS the
	// character rather than the ability system's owner.
	for (AActor* Enemy : UCataclysmTargeting::FindEnemiesInSphere(
			 Character->GetWorld(), Character, Character->GetActorLocation(),
			 RadiusMetres * CentimetresPerMetre))
	{
		// A REAL HIT, so the target's armour, resistance, evasion and block all
		// apply and what lands is smaller than what was sent. That is the
		// opposite of retaliation, which the design says explicitly is not a
		// hit, and it is why this uses `ApplyDirectDamage` rather than
		// `ReduceHealthDirectly`.
		//
		// NO DELIVERY FLAGS. This is the character's own effect rather than a
		// minion's, so it may critically strike, may leech and does provoke
		// retaliation, exactly as one of its skills would.
		UCataclysmSkillEffects::ApplyDirectDamage(Character, Enemy, Amount);
	}

	// AND THE INTERVAL RESTARTS WHETHER OR NOT ANYTHING WAS STANDING IN IT. See
	// the header: "you release a nova every 5 seconds" does not make the release
	// depend on there being a target.
	AbilitySystem->NoteNovaReleased(IntervalSeconds);

	return Amount;
}
