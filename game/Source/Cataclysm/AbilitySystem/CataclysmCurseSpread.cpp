// Copyright Stephen Dubois. All Rights Reserved.

#include "AbilitySystem/CataclysmCurseSpread.h"
// For the Fervour a death under a paying curse earns its caster. Issue #2885, N6.
#include "AbilitySystem/CataclysmFervour.h"
#include "AbilitySystem/CataclysmKillFervour.h"
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
	// which matches the one-stack rule the design states for ailments and damage
	// over time.
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

	// AND NOTHING A DEATH WOULD PAY, until the caller says otherwise. Issue #2885, N6: a curse stating `KillFervour`
	// writes it back after this call (`UCataclysmSkillTemplate::ApplyNamedEffectsTo`, and `SpreadFromDying` for the
	// next holder); every other refresh -- an Anathema over a Death Warrant -- leaves a mark that pays nothing.
	Mark->KillFervour = 0.0f;
	Mark->SkillName.Reset();
	Mark->Curses.Reset();
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
	// the curse it already paid for from passing on.
	AActor* Credited = Mark->Caster.IsValid() ? Mark->Caster.Get() : Dying;

	// WHAT A PAYING CURSE CARRIES, copied before anything below can refresh this mark. Issue #2885, N6.
	const float KillFervour = Mark->KillFervour;
	const FString PayingSkill = Mark->SkillName;
	const TArray<FGameplayTag> PayingCurses = Mark->Curses;

	// AND A DEATH WHILE THE CURSE HOLDS PAYS ITS CASTER, whether or not anything is in reach to pass it to. Issue #2885,
	// the Death draft's N6, slice DS3: the Sword's Death Warrant, "if it dies while the warrant holds, you gain 10
	// Fervour". HOLDS means the dying creature still carries one of the curses that skill laid, laid by that caster, so
	// a warrant that ran out or was cleansed pays nothing. ONLY WHILE THE CASTER IS HERE: a caster gone has no pool.
	if (KillFervour > 0.0f && Mark->Caster.IsValid()
		&& UCataclysmKillFervour::CarriesACurseLaidBy(Dying, PayingCurses, Mark->Caster.Get()))
	{
		const float Paid = UCataclysmFervour::GainStated(
			UCataclysmTargeting::AbilitySystemOf(Mark->Caster.Get()), KillFervour);
		UE_LOG(LogCataclysm, Display,
			TEXT("%s died under %s's curse, which holds: %.1f Fervour paid of the %.1f its row states."),
			*GetNameSafe(Dying), *PayingSkill, Paid, KillFervour);
	}

	// "THE NEAREST LIVING ENEMY" IS AN ENEMY OF THE CASTER'S SIDE, which is the
	// dying creature's own side. With the caster here, that is the caster's
	// enemies. With the caster gone, it is the dying creature's ALLIES: its
	// enemies are the player's side, and asking for them passed a curse whose
	// caster had gone to the nearest of the player's own characters. Ruled
	// 2026-10-31, when passed curses began to pass again and that fault would
	// have run from one of the player's characters to the next.
	TArray<AActor*> Nearby = Mark->Caster.IsValid()
		? UCataclysmTargeting::FindEnemiesInSphere(
			Dying->GetWorld(), Credited, Dying->GetActorLocation(), Mark->RangeCm)
		: UCataclysmTargeting::FindAlliesInSphere(
			Dying->GetWorld(), Dying, Dying->GetActorLocation(), Mark->RangeCm);

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
		UCataclysmCurseSpread* Passed = UCataclysmCurseSpread::MarkOn(Next[0], Mark->Caster.Get(), Mark->RangeCm);

		// AND WHAT ITS DEATH WOULD PAY GOES WITH IT, so each holder in the chain is worth the row's figure: "the warrant
		// passes to the nearest enemy". The copies `CopyDebuffsTo` laid are credited to the caster, so the next death
		// is asked "does the warrant hold" of the same curses. Issue #2885, N6.
		if (Passed && KillFervour > 0.0f)
		{
			Passed->KillFervour = KillFervour;
			Passed->SkillName = PayingSkill;
			Passed->Curses = PayingCurses;
		}

		UE_LOG(LogCataclysm, Verbose,
			TEXT("%s died carrying a spreading curse; %d applications passed to "
				 "%s."),
			*GetNameSafe(Dying), Applied, *GetNameSafe(Next[0]));
	}

	return Applied;
}
