// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CataclysmRisingCurse.generated.h"

class ACataclysmMinion;
struct FCataclysmWeaponSkill;

/**
 * Marks a cursed creature as one that rises for its curser when it dies under the curse. Issue #2885, the Death
 * draft's N5, slice DS3: `OnDeath=Rise` and `RisesAs=<kind>` on a Debuff row.
 *
 * WHAT ASKS FOR IT. The Greataxe's Death Rattle: "any that dies while it rattles rises at once as a husk, the same husk
 * Shallow Graves raises, that fights for you and turns the enemies around it on itself. Each husk lasts 8 seconds and
 * reserves Fervour while it stands." And the Wand's Soul Snare, whose wisp is Grave Mouth's, which waits with that row.
 *
 * THE MINION IS READ FROM THE SUMMON ROW THAT RAISES ITS KIND, as a risen imp is read from Summon Imp's
 * (`UCataclysmRisenImps`): the one Summon row of the curse's damage type whose `Minions` names the kind `RisesAs` names
 * -- Shallow Graves for the husk. Its `Duration` is how long the risen one lasts, its `FervourReserve` what it holds
 * back, its `Radius` its explosion's and its taunt's reach, its `Taunts=1` and `Burn=1` whether it taunts and burns. So
 * Death Rattle's husk is Shallow Graves' husk in every figure, which is what its sentence says. The generator refuses a
 * `RisesAs` no such row raises (`validate_risen_kinds`). Read when the curse is laid, so the death needs no table.
 *
 * "WHILE IT RATTLES": the dying creature must still carry one of the curses the row laid, laid by its caster
 * (`UCataclysmKillFervour::CarriesACurseLaidBy`), so a curse that ran out or was cleansed raises nothing.
 *
 * A COMPONENT, FOR THE REASON `UCataclysmCurseSpread` GIVES: the instruction outlives the skill that gave it, and a
 * component is destroyed with its creature. ONE PER CREATURE, REFRESHED RATHER THAN ADDED TO.
 *
 * NOTHING RISES WHEN THE POOL CANNOT HOLD ITS RESERVE, by the owner's ruling of 2026-11-20 (issue #2787): every minion
 * reserves, and the reserve is the only limit on how many stand.
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmRisingCurse : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Mark this creature, or refresh its mark, with what rises from it: the kind, read with every figure from the Summon
	 * row of `DamageType` that raises it.
	 *
	 * @param Target      the cursed creature
	 * @param Caster      who cursed it: whom the risen one fights for
	 * @param Kind        `RisesAs`, a Minion Types row
	 * @param DamageType  the cursing skill's damage type, whose Summon row is read
	 * @param Curses      the curses the row laid, which "while it rattles" asks about
	 * @return the mark, or null when no Summon row of that damage type raises the kind, which is logged
	 */
	static UCataclysmRisingCurse* MarkOn(AActor* Target, AActor* Caster, const FString& Kind, const FString& DamageType,
										 const TArray<FGameplayTag>& Curses);

	/**
	 * Raise what this creature's mark names, where it died, for its curser, if the curse still holds and the curser's
	 * pool can hold the reserve. CALLED FROM `ACataclysmEnemyCharacter::HandleDeath`, beside the Ritualist's risen imp,
	 * outside the killer's block: the sentence asks who laid the curse, not who dealt the last blow. The curse is read
	 * off the creature's own running effects, which `UCataclysmSkillEffects::MarkDead` leaves in place.
	 *
	 * @return the risen minion, or null when nothing rose
	 */
	static ACataclysmMinion* RiseFromDying(AActor* Dying);

	/**
	 * The one Summon row of `DamageType` whose `Minions` names `Kind` with a `Duration` of its own, not possessing.
	 * @return whether one was found, written to `OutRow`
	 */
	static bool FindTheRowThatRaises(const FString& DamageType, const FString& Kind, FCataclysmWeaponSkill& OutRow);

	/** Whom the risen one fights for. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	TWeakObjectPtr<AActor> Caster;

	/** The kind that rises, a Minion Types row: `Husk` for Death Rattle. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	FString Kind;

	/** The Summon row it is read from, by name, for the log: Shallow Graves for the husk. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	FString FromRow;

	/** How long the risen one lasts, the row's `Duration`, in seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	float LifetimeSeconds = 0.0f;

	/** What one risen holds back of its curser's Fervour, as the row states it, before `UCataclysmCommand::ReserveFor`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	float StatedReserve = 0.0f;

	/** The row's tags, which the reserve's reduction is asked with, as a summon's is. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	FGameplayTagContainer RowTags;

	/** The row's `Radius`, in centimetres: the risen one's explosion's reach, and its taunt's. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	float RadiusCm = 0.0f;

	/** Whether the risen one taunts the enemies within `RadiusCm` of it, the row's `Taunts=1` (C3). */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	bool bTaunts = false;

	/** Whether the risen one sets alight what it strikes, the row's `Burn=1`. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	bool bBurns = false;

	/** The curses the cursing row laid, which "while it rattles" asks about. */
	UPROPERTY(BlueprintReadOnly, Category = "Cataclysm|Rising Curse")
	TArray<FGameplayTag> Curses;
};
