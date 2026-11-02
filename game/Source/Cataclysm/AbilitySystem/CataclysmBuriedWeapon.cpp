// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmBuriedWeapon.h"
// For what a blow resolved to, so a burn is refused on an evaded one.
// Issue #1156.
#include "AbilitySystem/CataclysmDamageCalculation.h"
// For the Fervour the axe's next landing earns its thrower. Issue #1515.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "GameFramework/Actor.h"

UCataclysmBuriedWeapon* UCataclysmBuriedWeapon::BuryIn(
	AActor* Target, AActor* Caster, float RangeCm, float InDamagePercent,
	const FGameplayTagContainer& InSkillTags, bool bInBurns, const TWeakObjectPtr<AActor>& InCastBy)
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
	Buried->CastBy = InCastBy;
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

	// WHICH SIDE, ASKED BEFORE THE HIT as a fired contact asks it. Issue #1938.
	const bool bFromBehind = UCataclysmSkillEffects::IsBehind(Credited, NextHost);

	// AND WHO THE THROWING USE WAS CAST FROM, so a skill gadget's axe is still priced as the gadget's while the gadget
	// stands. Empty for every axe a player threw by hand, which is the delivery this always sent. Stack 2 (P5). AND
	// ONLY WHILE THE THROWER IS HERE: when it has gone the dying creature is credited, and its blow is no gadget's.
	// AND AS IT WAS KEPT, NOT THROUGH `Get`, so a skill trap used up before this leap is still named by its identity and
	// the leap is priced and announced as the trap's (ruled 2026-10-21, #2462). A gadget gone still answers null to
	// `SkillGadgetCasting`, so its axe is the player's then, as before (P5).
	FCataclysmHitDelivery Delivery;
	Delivery.CastBy = Buried->Caster.IsValid() ? Buried->CastBy : TWeakObjectPtr<AActor>();
	// AND THE THROWING SKILL'S TAGS, AS THE BLOW OF THAT SKILL IT IS, so a kill this leap makes carries them as a cast's
	// blow's kill carries its skill's, and a `kill` row requiring a tag answers it. Ruled 2026-10-22 (#2465, N1 (a),
	// N2 (a)). The skill object may be long gone, so the tags kept here are named rather than the skill. A player's own
	// axe and a trap's alike; melee or ranged was already read from these tags by `ApplyHit` (N3 (a)).
	Delivery.SkillTagsWithNoSkill = &Buried->SkillTags;

	FCataclysmDamageResult Resolved;
	const float Dealt = UCataclysmSkillEffects::ApplyHit(
		Credited, NextHost, Buried->DamagePercent, Buried->SkillTags,
		Delivery, &Resolved);

	// A DESIGNED BURN, because `bBurns` was copied from the throwing row's own
	// `Burn=1`. Issue #917: a skill that states an ailment applies it whether or
	// not the blow hurt.
	//
	// THE COMMENT THAT STOOD HERE SAID A BURN IS A SHARE OF THE HIT. That
	// stopped being true on 2026-08-24, when burn became a flat 25 a second.
	//
	// AN EVADED BLOW SETS NOTHING ALIGHT. Issue #1156, decided on 2026-09-04:
	// evasion says the blow never connected, so nothing it carries arrives. One
	// armour stopped dead still burns, because armour answers whether the blow
	// hurt rather than whether it landed. `Dealt` cannot be used to ask this --
	// it is the figure that was SENT, and it is positive for an evaded blow.
	//
	// AND THE THROWING SKILL'S TAGS, AS THE LEAP'S HIT CARRIES THEM, so a kill by a tick of this burn carries them as
	// the leap's own kill does, and no skill name. Issue #2683, under #2465's ruling N1 (a). Identity only: the burn is
	// priced as it was, with no skill (ruled 2026-11-01, J1).
	if (Buried->bBurns && !Resolved.bEvaded)
	{
		UCataclysmSkillEffects::ApplyBurn(Credited, NextHost, Dealt,
										  /*bScalesWithInstigator=*/true,
										  /*bBurnIsDesigned=*/true,
										  /*DealtBy=*/nullptr,
										  /*Skill=*/nullptr,
										  /*SkillTagsWithNoSkill=*/&Buried->SkillTags);
	}

	// AND THE FERVOUR THE LANDING EARNS WHOEVER THREW THE AXE. Issue #1515, the
	// Ravager's starting node: "1 for each enemy your attacks hit". This blow
	// never passes through `UCataclysmSkillTemplate::HitTargets`, so it pays
	// here, by the same test: it landed and it carried damage. ONLY WHILE THE
	// THROWER IS STILL HERE: when it has gone, `Credited` is the dying creature,
	// and a creature's blows earn nothing.
	if (Buried->Caster.IsValid() && Dealt > 0.0f && !Resolved.bEvaded)
	{
		UCataclysmFervour::GainForEnemiesHit(
			UCataclysmTargeting::AbilitySystemOf(Credited), Buried->SkillTags,
			/*EnemiesHit=*/1);

		// AND THE THROWER'S RUNNING BUFFS ARE TOLD, BY THE SAME TEST AND ONLY
		// WHILE THE THROWER IS HERE. Issue #1938, ruled 2026-09-24: this blow
		// never passes through `HitTargets`. When the thrower has gone the
		// credited attacker is the dying creature, whose blows tell nothing.
		UCataclysmSkillTemplate::NoteBlowLanded(
			Credited, NextHost, NextHost->GetActorLocation(), bFromBehind);
	}

	// AND IT IS NOW IN THAT ONE, which is what makes it go on. Copied from the
	// old component rather than from the skill, because the skill ended long
	// ago: a Harrower thrown ten seconds back is not running any more.
	UCataclysmBuriedWeapon* Moved = BuryIn(NextHost, Credited, Buried->RangeCm,
										  Buried->DamagePercent,
										  Buried->SkillTags, Buried->bBurns,
										  Buried->CastBy);
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
