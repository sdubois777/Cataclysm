// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmBuriedWeapon.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "GameFramework/Actor.h"

UCataclysmBuriedWeapon* UCataclysmBuriedWeapon::BuryIn(
	AActor* Target, AActor* Caster, float RangeCm, float InDamagePercent,
	const FGameplayTagContainer& InSkillTags, bool bInBurns)
{
	if (!IsValid(Target) || RangeCm <= 0.0f)
	{
		return nullptr;
	}

	// REFRESHED RATHER THAN ADDED TO. Two Harrowers in one creature are one
	// buried axe, which matches the single-stack rule every player-applied
	// effect follows.
	UCataclysmBuriedWeapon* Buried =
		Target->FindComponentByClass<UCataclysmBuriedWeapon>();
	if (!Buried)
	{
		Buried = NewObject<UCataclysmBuriedWeapon>(Target);
		if (!Buried)
		{
			return nullptr;
		}
		Buried->RegisterComponent();
	}

	Buried->RangeCm = RangeCm;
	Buried->DamagePercent = InDamagePercent;
	Buried->SkillTags = InSkillTags;
	Buried->bBurns = bInBurns;
	Buried->Caster = Caster;
	return Buried;
}

bool UCataclysmBuriedWeapon::LeapFromDying(AActor* Dying)
{
	if (!IsValid(Dying))
	{
		return false;
	}

	const UCataclysmBuriedWeapon* Buried =
		Dying->FindComponentByClass<UCataclysmBuriedWeapon>();
	if (!Buried || Buried->RangeCm <= 0.0f)
	{
		// The ordinary case. Nothing but Harrower buries a weapon.
		return false;
	}

	// THE CASTER IS CREDITED, AND THE DYING CREATURE STANDS IN FOR IT WHEN IT IS
	// GONE. A player who threw the axe and then left, or died, should not stop
	// the axe already in the field from moving on. `FindEnemiesInSphere` needs
	// somebody to decide sides from, and the dying creature knows its own.
	AActor* Credited = Buried->Caster.IsValid() ? Buried->Caster.Get() : Dying;

	TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
		Dying->GetWorld(), Credited, Dying->GetActorLocation(), Buried->RangeCm);

	// NOT BACK INTO THE CORPSE. This runs before the death is recorded, so the
	// dying creature is still found by the search and is dropped by name.
	Nearby.Remove(Dying);
	if (Nearby.IsEmpty())
	{
		// "Until nothing is left in reach." The axe stops here, in the body it
		// was already in, which is destroyed with it.
		return false;
	}

	// THE NEAREST ONE, which is what the row says and which
	// `FindEnemiesInSphere` answers first.
	AActor* NextHost = Nearby[0];

	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Credited, NextHost, Buried->DamagePercent, Buried->SkillTags,
		FCataclysmHitDelivery());

	// A DESIGNED BURN, because `bBurns` was copied from the throwing row's own
	// `Burn=1`. Issue #917: a skill that states an ailment applies it whether or
	// not the blow hurt.
	//
	// THE COMMENT THAT STOOD HERE SAID A BURN IS A SHARE OF THE HIT. That
	// stopped being true on 2026-08-24, when burn became a flat 25 a second.
	if (Buried->bBurns)
	{
		UCataclysmSkillEffects::ApplyBurn(Credited, NextHost, Dealt,
										  /*bScalesWithInstigator=*/true,
										  /*bBurnIsDesigned=*/true);
	}

	// AND IT IS NOW IN THAT ONE, which is what makes it go on. Copied from the
	// old component rather than from the skill, because the skill ended long
	// ago: a Harrower thrown ten seconds back is not running any more.
	UCataclysmBuriedWeapon* Moved = BuryIn(NextHost, Credited, Buried->RangeCm,
										  Buried->DamagePercent,
										  Buried->SkillTags, Buried->bBurns);
	if (Moved)
	{
		Moved->Leaps = Buried->Leaps + 1;
	}

	UE_LOG(LogCataclysm, Verbose,
		TEXT("A buried weapon tore free of %s and struck %s for %.0f. Leap %d."),
		*GetNameSafe(Dying), *GetNameSafe(NextHost), Dealt,
		Moved ? Moved->Leaps : 0);

	return true;
}
