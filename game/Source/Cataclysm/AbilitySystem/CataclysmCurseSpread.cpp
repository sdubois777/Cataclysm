// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmCurseSpread.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "Cataclysm.h"
#include "GameFramework/Actor.h"

UCataclysmCurseSpread* UCataclysmCurseSpread::MarkOn(AActor* Target,
													 AActor* Caster,
													 float RangeCm)
{
	if (!IsValid(Target) || RangeCm <= 0.0f)
	{
		return nullptr;
	}

	// REFRESHED RATHER THAN ADDED TO. A second Anathema on the same creature
	// moves the range and the caster rather than leaving two components on it,
	// which matches the single-stack rule every player-applied effect follows.
	UCataclysmCurseSpread* Mark = Target->FindComponentByClass<UCataclysmCurseSpread>();
	if (!Mark)
	{
		Mark = NewObject<UCataclysmCurseSpread>(Target);
		if (!Mark)
		{
			return nullptr;
		}
		Mark->RegisterComponent();
	}

	Mark->RangeCm = RangeCm;
	Mark->Caster = Caster;
	return Mark;
}

int32 UCataclysmCurseSpread::SpreadFromDying(AActor* Dying)
{
	if (!IsValid(Dying))
	{
		return 0;
	}

	const UCataclysmCurseSpread* Mark =
		Dying->FindComponentByClass<UCataclysmCurseSpread>();
	if (!Mark || Mark->RangeCm <= 0.0f)
	{
		// The ordinary case. Only Anathema and the Staff's Quarry, and the
		// holders their curses passed to, are marked this way.
		return 0;
	}

	// THE CASTER IS CREDITED, AND THE DYING CREATURE STANDS IN FOR IT WHEN IT IS
	// GONE. A player who cursed a pack and then left, or died, should not stop
	// the curse it already paid for from passing on. `FindEnemiesInSphere` needs
	// somebody to decide sides from, and the dying creature knows its own.
	AActor* Credited = Mark->Caster.IsValid() ? Mark->Caster.Get() : Dying;

	TArray<AActor*> Nearby = UCataclysmTargeting::FindEnemiesInSphere(
		Dying->GetWorld(), Credited, Dying->GetActorLocation(), Mark->RangeCm);

	// NOT BACK ONTO THE CORPSE. This runs before the death is recorded, so the
	// dying creature is still found by the search and has to be dropped by name.
	Nearby.Remove(Dying);
	if (Nearby.IsEmpty())
	{
		return 0;
	}

	// THE NEAREST ONE, SINGULAR, which is what the row says: "passes the curse
	// to the nearest living enemy". `FindEnemiesInSphere` answers nearest first.
	// Passing it to everything in range would clear a room from one cast.
	const TArray<AActor*> Next = {Nearby[0]};

	const int32 Applied = UCataclysmSkillEffects::CopyDebuffsTo(
		Credited, Dying, Next,
		UCataclysmSkillEffects::DamageTypeOf(Credited));

	// AND THE NEW HOLDER PASSES IT ON IN TURN. Anathema: "anything that dies while damned passes the curse to the
	// nearest living enemy", and a creature the curse passed to is damned. Quarry: "if the marked enemy dies first, the
	// mark jumps to the nearest living enemy", and the mark on its new holder is still the mark. Ruled 2026-10-31, one
	// rule for both. The same caster and the same reach go with it; each copy lasts the effect's own designed duration.
	//
	// IT CANNOT RETURN TO A HOLDER IT LEFT. The search finds only the living, and this dying creature is dropped by
	// name above, so a chain ends when no living enemy is within reach.
	if (Applied > 0)
	{
		UCataclysmCurseSpread::MarkOn(Next[0], Mark->Caster.Get(), Mark->RangeCm);

		UE_LOG(LogCataclysm, Verbose,
			TEXT("%s died carrying a spreading curse; %d applications passed to "
				 "%s."),
			*GetNameSafe(Dying), Applied, *GetNameSafe(Next[0]));
	}

	return Applied;
}
