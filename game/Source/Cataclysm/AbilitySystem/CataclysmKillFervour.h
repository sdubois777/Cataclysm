// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CataclysmKillFervour.generated.h"

class AActor;
struct FCataclysmSkillShapeParams;

/**
 * Extra Fervour a skill pays for each enemy it kills, or that dies carrying its mark. Issue #2885, the Death draft's
 * N6, slice DS3: `KillFervour=N` on a skill row.
 *
 * WHAT ASKS FOR IT. Seven of the Death skills the owner approved on 2026-11-24: Grim Swath ("every enemy it kills
 * gives you 2 more Fervour"), Last Toll, Pale Rider and Restless Spirits pay for what their own blow kills; Death
 * Warrant ("if it dies while the warrant holds, you gain 10 Fervour") for a death under its curse; Hungering Soul
 * ("every host it leaves dead gives you 2 Fervour") for each host its soul leaves dead; Danse Macabre ("every enemy
 * that dies during the dance gives you 5 Fervour") for each death during the dance.
 *
 * FOUR ROUTES, ONE PAYMENT A DEATH FOR ONE SKILL. A kill by the skill's own blow is paid here, from
 * `UCataclysmSkillEffects::MarkDead`, the one place every death is recorded, BEFORE its hooks run. A death under a
 * paying curse is paid by `UCataclysmCurseSpread::SpreadFromDying`, a host's death by
 * `UCataclysmBuriedWeapon::LeapFromDying`, both hooks of the same function; and a death during a dance by
 * `UCataclysmMovementSkill::NoteADeathWhileItRuns`, told of every enemy death as Butcher's Heat is. So the blow's
 * payment here steps aside for a death the same skill's mark will pay (`AMarkOfTheSameSkillPaysFor`), and for a
 * flicker that counts deaths (`CountsTheDeathsWhileItRuns`): Death Warrant's own blow killing its warranted target,
 * or a second Hungering Soul killing the host of the first, is paid once.
 *
 * A KILL IS A BLOW'S, NOT A TICK'S. A judgement of slice DS3: the Necrosis Last Toll lays is the ailment's, and a
 * creature it finishes later was not killed by the toll. The rows whose sentences reach past the blow say so -- "dies
 * while the warrant holds", "every host it leaves dead", "dies during the dance" -- and are paid by their marks.
 *
 * NO STATE OF ITS OWN. Every figure is read off the skill's row and the dying creature's record of its last blow.
 */
UCLASS()
class CATACLYSM_API UCataclysmKillFervour : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Pay the skill whose blow killed `Dying` its row's `KillFervour`, and answer what arrived.
	 *
	 * CALLED FROM `UCataclysmSkillEffects::MarkDead`, before its hooks run and so while a mark is still on the dying
	 * creature to be asked about.
	 *
	 * NOTHING UNLESS: the last blow on record was lethal (its `KillingBlowDamage` is written for a lethal blow alone),
	 * was a blow and not a tick, was not a point-blank skill's share struck at an ally, and was a skill's whose row
	 * states `KillFervour` above nought; the skill is not a flicker counting deaths; its caster is still here and was
	 * the dying creature's enemy; and no mark of the same skill's on the dying creature pays for this death.
	 *
	 * @return the Fervour that arrived, after the generation rate and the clamp: nought for every other death
	 */
	static float PayForTheKillingBlow(AActor* Dying);

	/**
	 * Whether `Who` carries a running effect granting one of `Curses` whose instigator is `By`: "while the warrant
	 * holds", "while it rattles". The record `UCataclysmRisenImps::CurserOf` reads, asked for named curses. A curse that
	 * ran out or was cleansed is no longer carried, and one another source laid first and this caster only refreshed
	 * keeps that source as its instigator (the one-stack rule), so it does not count.
	 */
	static bool CarriesACurseLaidBy(const AActor* Who, const TArray<FGameplayTag>& Curses, const AActor* By);

	/**
	 * Whether a mark the skill named `SkillName` of `Caster`'s left on `Dying` pays for its death: a paying curse that
	 * still holds, or a buried weapon carrying `KillFervour`. When it does, the blow's payment steps aside.
	 */
	static bool AMarkOfTheSameSkillPaysFor(const AActor* Dying, const AActor* Caster, const FString& SkillName);

	/**
	 * Whether a skill's row counts every death while it runs, rather than paying for its own blow's kills: a flicker
	 * stating `ScalingSource=Kill`, Danse Macabre's. `UCataclysmMovementSkill::NoteADeathWhileItRuns` pays it.
	 */
	static bool CountsTheDeathsWhileItRuns(const FCataclysmSkillShapeParams& Params);
};
