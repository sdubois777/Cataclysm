// Copyright Stephen Dubois. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AbilitySystem/CataclysmAbilitySet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Items/CataclysmItem.h"
#include "CataclysmWeaponSlotsComponent.generated.h"

class UCataclysmAbilitySystemComponent;
class UDataTable;

/**
 * Fills the seven ability slots from the equipped weapon, and refills them when
 * the weapon changes.
 *
 * WHAT MAKES A GEAR DROP MEAN SOMETHING. Issue #36: which ability sits in each
 * slot is decided entirely by the equipped weapon's type and damage type.
 * Picking up a different weapon replaces every ability. Nothing in C++ names
 * a skill and nothing decides which skill fills which slot -- both come out of
 * game/Data/WeaponSkills.csv, generated from the Weapon Skills sheet of
 * docs/All_Things_Cataclysm.xlsx. Changing what a Dagger offers is a workbook
 * edit.
 *
 * SIX OF THE SEVEN COME FROM THE MATRIX AND THE SEVENTH DOES NOT. The basic
 * attack is weapon damage itself, so it does not vary by damage type and is read
 * from the weapon's own row on the Item Bases sheet instead. Issue #524. That
 * makes it the one slot a weapon fills even when its damage type covers none of
 * its skills, and the one slot a Shield does not fill, because a Shield grants no
 * attack damage. Issue #619.
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
	/**
	 * What a skill's base critical strike chance is when it names none. 5%.
	 *
	 * THE DESIGN PUTS THIS ON THE SKILL AND NOT ON THE CHARACTER. Its stat source
	 * table says "the skill being used" supplies critical strike chance, and the
	 * sentence after it is explicit: "A character has no critical strike chance
	 * in the abstract." So this is written when a weapon's skills are granted and
	 * zeroed when they are taken away, the same way the weapon's own rate is.
	 *
	 * WHY 5%: it is Path of Exile's base for a plain melee weapon and it is
	 * already what this project gives an ordinary enemy, so a player and a
	 * monster start from the same place. Decided on 2026-08-04 and recorded in
	 * `docs/DECISIONS.md`; the same number is `DEFAULT_SKILL_CRIT_CHANCE` in
	 * `sim/cataclysm_sim/character.py`, and
	 * `tools/tests/test_the_critical_strike_numbers_are_one_set.py` fails if the
	 * two ever disagree.
	 *
	 * IT IS A DEFAULT AND NOT A FLOOR. A skill stating 1% is meant to get 1%.
	 * Nothing states one yet, because `game/Data/WeaponSkills.csv` has no column
	 * for it -- all 558 rows use this. Issue #657.
	 */
	static constexpr float DefaultSkillCritChancePercent = 5.0f;

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

	/**
	 * Equips whatever StartingWeaponType names, and fills every slot from it.
	 *
	 * WHY THIS EXISTS SEPARATELY FROM EquipWeaponType. Which weapon a character
	 * begins with is data on this component; WHEN it can be equipped is the
	 * pawn's business, because the ability system lives on the player state and
	 * does not exist until possession completes. So the pawn calls this once it
	 * has wired the ability system up, and this component does not have to know
	 * anything about possession order.
	 *
	 * @return how many slots were filled, or 0 if no starting weapon is named
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Weapon")
	int32 EquipStartingWeapon();

	/** Empties every slot this component filled. */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Weapon")
	void UnequipWeapon();

	/** Which weapon type a character begins holding. See the field for why. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	const FString& GetStartingWeaponType() const { return StartingWeaponType; }

	/** Which damage type's skills are in use. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	const FString& GetDamageType() const { return DamageType; }

	/** The weapon type currently equipped. Empty when nothing is. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	const FString& GetEquippedWeaponType() const { return EquippedWeaponType; }

	/** What the equipped weapon offers, whether or not it was granted. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	const TArray<FCataclysmWeaponSkill>& GetAvailableSkills() const { return AvailableSkills; }

	/**
	 * The weapon sub-type this character's hits carry: Piercing, Slashing,
	 * Blunt, Magic, or empty for none.
	 *
	 * WHAT IT DECIDES. Slashing deals 10% more to health, magic strips 10% more
	 * energy shield, and piercing ignores 20% of the target's armour. All three
	 * were applied correctly by `UCataclysmDamageCalculation::Resolve` and none
	 * was ever set, because nothing joined the equipped weapon to a hit. Issue
	 * #639. Blunt's 10% chance to stun needs a mechanism that does not exist and
	 * is not built here.
	 *
	 * READ OFF THE ITEM BASES TABLE, not stored. `game/Data/ItemBases.csv`
	 * carries a sub-type for all fourteen weapon types and is where the design
	 * edits it, so a copy here would be a second answer that could disagree.
	 *
	 * ONE WEAPON TODAY, AND THE RULE FOR TWO IS ALREADY DECIDED. This component
	 * equips a single weapon type, so there is nothing to reconcile yet. When it
	 * holds two, the project owner's ruling of 2026-08-16 applies: every weapon
	 * actually SWUNG has to agree or the hit has no sub-type, and a Shield does
	 * not vote because it is not swung. `subtype_of` in
	 * `sim/cataclysm_sim/player_damage.py` is that rule and states why.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	FString GetEquippedSubType() const;

	/**
	 * The sub-type an actor's hits carry, or empty if it has no weapon at all.
	 *
	 * A STATIC ON THIS COMPONENT so the attribute set that resolves a hit can
	 * ask without knowing how a character stores its weapons. An actor with no
	 * weapon slots component answers empty, which is the answer every hit gave
	 * before this existed -- so an enemy, which has no such component, keeps
	 * hitting exactly as it did.
	 */
	static FString SubTypeOf(const AActor* Actor);

	/**
	 * Uses this matrix instead of the generated one.
	 *
	 * For tests, which build a table they control so a case can be set up that
	 * the real matrix does not contain.
	 */
	void SetWeaponSkillTable(const UDataTable* Table) { WeaponSkillTable = Table; }

	/** Which damage type's skills to use. For tests; see the field below. */
	void SetDamageType(const FString& NewDamageType) { DamageType = NewDamageType; }

	/** Which weapon a character begins holding. For tests; see the field below. */
	void SetStartingWeaponType(const FString& NewType) { StartingWeaponType = NewType; }

	/** Uses this item base table instead of the generated one. For tests. */
	void SetItemBaseTable(const UDataTable* Table) { ItemBaseTable = Table; }

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
	 * damage types yet. When items carry their rolled type this reads it from the
	 * equipped item instead.
	 *
	 * DEMONIC, BECAUSE THAT IS WHAT THE VERTICAL SLICE IS. The design document's
	 * Phase 1 roadmap names the Demonic Cataclysm, the Demonic Masochist tree and
	 * Demonic skills across three weapon types, and issue #61 established that
	 * the Cataclysm being fought decides the player's damage type. Shipping War
	 * here would drop loot the slice's player content cannot use.
	 *
	 * ONLY THREE DEMONIC WEAPONS ARE DESIGNED: Greataxe, Fist and Staff, one for
	 * each Demonic class. The other seven weapons Demonic can roll on have rows
	 * in the matrix with no skill on them, so they grant nothing. That is
	 * visible rather than hidden, and it is issue #62's remaining scope.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Weapon")
	FString DamageType = TEXT("Demonic");

	/**
	 * Which weapon type a character begins holding.
	 *
	 * TEMPORARY, IN THE SAME WAY DamageType ABOVE IS TEMPORARY, and it stands in
	 * for a system the design already specifies. The design document says
	 * "when starting a new character, players choose a starting weapon type and
	 * damage type, which determines their initial skill set and first available
	 * passive class tree". That chooser is the character creator, which is issue
	 * #50 and does not exist. Until it does, every character begins with this.
	 *
	 * WITHOUT IT NOTHING EQUIPPED A WEAPON AT ALL. EquipWeaponType had no caller
	 * outside the automation tests, so a play session filled no ability slot and
	 * every skill key reached nothing -- however many skills were designed and
	 * implemented. That was issue #169.
	 *
	 * GREATAXE, because the vertical slice is Demonic and the Greataxe is the
	 * Ravager's weapon, the first of the three the slice designs. Any of the ten
	 * weapon types Demonic covers would work; a type it does NOT cover would
	 * grant nothing at all, and
	 * Cataclysm.WeaponSlots.TheStartingWeaponActuallyGrantsSkills refuses that.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Weapon")
	FString StartingWeaponType = TEXT("Greataxe");

	/** Granted for each filled slot until the real skills have numbers. */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Weapon")
	TSubclassOf<UCataclysmGameplayAbility> UndesignedSkillClass;

	/** The item base table, for the equipped weapon's sub-type and basic attack. */
	UPROPERTY(Transient)
	TObjectPtr<const UDataTable> ItemBaseTable;


private:
	UCataclysmAbilitySystemComponent* GetAbilitySystem() const;

	// THIS COMPONENT NO LONGER WRITES ATTACK DAMAGE OR ATTACK SPEED, and it did
	// until issue #845. It wrote them from the equipped weapon TYPE, which meant
	// a second worn weapon changed nothing and an upgrade level never applied
	// (issue #840), and it also meant two things wrote the same attribute, which
	// is why a weapon's attack damage affixes were gathered and then dropped.
	// UCataclysmPlayerClassStats::ApplyTo is the only writer now, and a weapon's
	// damage reaches it as an ordinary modifier from
	// UCataclysmEquipmentComponent::GatherModifiers. What is left here is which
	// SKILLS a weapon grants.


	UPROPERTY() FString EquippedWeaponType;
	UPROPERTY() TArray<FCataclysmWeaponSkill> AvailableSkills;

	/** What the current weapon granted, so it can all be taken back. */
	FCataclysmAbilitySetHandles GrantedHandles;
};
