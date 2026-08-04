// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "CataclysmWeaponSlotsComponent.generated.h"

class UCataclysmAbilitySystemComponent;
class UDataTable;

/**
 * Fills the six ability slots from the equipped weapon, and refills them when
 * the weapon changes.
 *
 * WHAT MAKES A GEAR DROP MEAN SOMETHING. Issue #36: which ability sits in each
 * slot is decided entirely by the equipped weapon's type and damage type.
 * Picking up a different weapon replaces all six abilities. Nothing in C++ names
 * a skill and nothing decides which skill fills which slot -- both come out of
 * game/Data/WeaponSkills.csv, generated from the Weapon Skills sheet of
 * docs/All_Things_Cataclysm.xlsx. Changing what a Dagger offers is a workbook
 * edit.
 *
 * WHAT IT GRANTS TODAY. UCataclysmUndesignedSkill, one per filled slot. The 61
 * designed skills have names, descriptions and tags but no numbers, so there is
 * no behaviour to build yet. The slot is occupied, the key that names it reaches
 * something, and swapping weapons visibly changes what is granted. When a skill
 * gains numbers, this grants its real class instead and nothing else here
 * changes.
 *
 * NOT EVERY WEAPON HAS SKILLS, AND THAT IS THE DESIGN. Each damage type has its
 * own list of weapon types. War covers twelve and includes neither the Wand nor
 * the Staff, so a War Wand grants nothing. That is correct, not a failure, so it
 * is reported at Verbose rather than as a warning.
 */
UCLASS(ClassGroup = (Cataclysm), meta = (BlueprintSpawnableComponent))
class CATACLYSM_API UCataclysmWeaponSlotsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCataclysmWeaponSlotsComponent();

	/**
	 * Equips a weapon type and refills every slot from it.
	 *
	 * Passing the type already equipped still refills, because the reason to
	 * call it twice is that something about the weapon changed.
	 *
	 * @return how many slots were filled
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Weapon")
	int32 EquipWeaponType(const FString& NewWeaponType);

	/** Empties every slot this component filled. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Weapon")
	void UnequipWeapon();

	/** The weapon type currently equipped. Empty when nothing is. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	const FString& GetEquippedWeaponType() const { return EquippedWeaponType; }

	/** What the equipped weapon offers, whether or not it was granted. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	const TArray<FCataclysmWeaponSkill>& GetAvailableSkills() const { return AvailableSkills; }

	/**
	 * Uses this matrix instead of the generated one.
	 *
	 * For tests, which build a table they control so a case can be set up that
	 * the real matrix does not contain.
	 */
	void SetWeaponSkillTable(const UDataTable* Table) { WeaponSkillTable = Table; }

	/** Which damage type's skills to use. For tests; see the field below. */
	void SetDamageType(const FString& NewDamageType) { DamageType = NewDamageType; }

protected:
	/**
	 * The weapon skill matrix. Loaded from the generated CSV on first use when
	 * nothing has set one.
	 */
	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> WeaponSkillTable;

	/**
	 * Which damage type's skills to use.
	 *
	 * TEMPORARY, AND IT IS THE ONE THING HERE THAT IS NOT DATA-DRIVEN. A weapon's
	 * damage type is rolled when the item drops, and items do not carry rolled
	 * damage types yet. War is used because it is the only damage type whose
	 * skills are designed: all 61 of them. When items carry their rolled type
	 * this reads it from the equipped item instead.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Weapon")
	FString DamageType = TEXT("War");

	/** Granted for each filled slot until the real skills have numbers. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Weapon")
	TSubclassOf<UCataclysmGameplayAbility> UndesignedSkillClass;

private:
	UCataclysmAbilitySystemComponent* GetAbilitySystem() const;

	UPROPERTY() FString EquippedWeaponType;
	UPROPERTY() TArray<FCataclysmWeaponSkill> AvailableSkills;

	/** What the current weapon granted, so it can all be taken back. */
	FCataclysmAbilitySetHandles GrantedHandles;
};
