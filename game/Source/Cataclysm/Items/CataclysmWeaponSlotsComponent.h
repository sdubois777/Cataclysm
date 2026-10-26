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
 * EVERY WORN WEAPON AND EVERY CARRIED DAMAGE TYPE, SINCE ISSUE #837 LAYER 2. The
 * skills form a pool, every combination of a worn weapon type and a damage type
 * the worn weapons carry, and each slot takes the first entry of the pool
 * designed for it: the creation type's first, then the worn weapons' types, and
 * within a type the first worn weapon's. The player cannot choose yet.
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
	 * ONE WEAPON TYPE, `EquipWeaponTypes` WITH THAT ONE, since issue #837 layer 2.
	 * The damage types are the ones this component already holds.
	 *
	 * @return how many slots were filled
	 */
	UFUNCTION(BlueprintCallable, Category = "Cataclysm|Weapon")
	int32 EquipWeaponType(const FString& NewWeaponType);

	/**
	 * Equips every worn weapon type at once and refills every slot from the pool they make with the damage types this
	 * component holds. Issue #837 layer 2.
	 *
	 * THE POOL IS EVERY COMBINATION (`UCataclysmWeaponSkills::PoolFor`) AND THE SLOTS TAKE ITS DEFAULT FILL
	 * (`UCataclysmWeaponSkills::DefaultFillFrom`): for each slot, the first pool entry designed for it. So the order of
	 * both lists decides which skill fills a slot two entries could: hand the weapon types in slot order (`WornWeapons`,
	 * the first OCCUPIED slot first) and the damage types in fill order (`InFillOrder`).
	 *
	 * THE BASIC ATTACK STAYS THE FIRST WEAPON'S, as it was: the first entry is what `GetEquippedWeaponType` names, and
	 * the sub-type and the hands are read off it as before.
	 *
	 * @return how many slots were filled
	 */
	int32 EquipWeaponTypes(const TArray<FString>& NewWeaponTypes);

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

	/**
	 * The first of the damage types in use, or empty when none is. Since issue #837 layer 2 a character can carry
	 * several; this is the first in fill order, the creation type when it is carried. `GetDamageTypes` has them all.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	FString GetDamageType() const { return DamageTypes.IsEmpty() ? FString() : DamageTypes[0]; }

	/** Every damage type whose skills join the pool, in fill order. Issue #837 layer 2. */
	const TArray<FString>& GetDamageTypes() const { return DamageTypes; }

	/** The weapon type currently equipped. Empty when nothing is. With two worn, the first occupied slot's. */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	const FString& GetEquippedWeaponType() const { return EquippedWeaponType; }

	/** Every weapon type whose skills join the pool, in slot order, as `EquipWeaponTypes` was handed them. */
	const TArray<FString>& GetWornWeaponTypes() const { return WornWeaponTypes; }

	/**
	 * What fills the slots: the basic attack and, for each other slot, the chosen skill or the default fill's. Granted
	 * or not. Each entry's `Slot` is the key it sits on and `DesignedSlot` the slot it was made for (layer 3).
	 *
	 * ONE PER SLOT, AS IT ALWAYS WAS, and every reader that looks a held skill up here keeps meaning what it meant:
	 * the skill bar, a triggered skill's held pool, a minion's copy. The whole pool is `GetSkillPool`.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	const TArray<FCataclysmWeaponSkill>& GetAvailableSkills() const { return AvailableSkills; }

	/**
	 * Every skill the worn weapons and the carried damage types make available, more than one to a slot when two
	 * weapons or two types are worn (`UCataclysmWeaponSkills::PoolFor`). Issue #837 layer 2. Since layer 3 the player
	 * chooses from it (`ACataclysmPlayerState::AssignSkill`), and every key nothing was chosen for takes its default.
	 */
	const TArray<FCataclysmWeaponSkill>& GetSkillPool() const { return SkillPool; }

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
	 * ONE WEAPON TODAY, AND THE RULE FOR TWO IS ALREADY DECIDED. This reads the
	 * first worn weapon type alone; since issue #837 layer 2 the component holds
	 * every worn type for the skill pool, and this was not changed with it. When it
	 * reads two, the project owner's ruling of 2026-08-16 applies: every weapon
	 * actually SWUNG has to agree or the hit has no sub-type, and a Shield does
	 * not vote because it is not swung. `subtype_of` in
	 * `sim/cataclysm_sim/player_damage.py` is that rule and states why.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	FString GetEquippedSubType() const;

	/**
	 * How many hands the equipped weapon takes, or -1 when that is not known.
	 * Issue #1515, for the condition `wielding_two_handed_weapon`.
	 *
	 * READ OFF THE ITEM BASES TABLE the way `GetEquippedSubType` above reads the
	 * sub-type, from the row whose `WeaponType` matches, and for its reason: the
	 * design edits `Hands` there.
	 *
	 * -1 FOR NO WEAPON, NO TABLE, OR A WEAPON TYPE THE TABLE HAS NO ROW FOR. A
	 * missing row is not guessed to be either, so the condition refuses it.
	 */
	UFUNCTION(BlueprintPure, Category = "Cataclysm|Weapon")
	int32 GetEquippedWeaponHands() const;

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
	 * The Cataclysm damage type an actor's own hits carry, or empty when it has
	 * no weapon slots at all. Issue #975.
	 *
	 * A STATIC FOR THE SAME REASON `SubTypeOf` ABOVE IS ONE: the attribute set
	 * that resolves a hit needs to know whether the hit's type is the
	 * character's own, and should not have to know how a character stores its
	 * weapons.
	 *
	 * AN ENEMY ANSWERS EMPTY, because it has no weapon slots component. That is
	 * right rather than a gap: an enemy's own type lives on the enemy and is
	 * read by `UCataclysmSkillEffects::DamageTypeOf`, which answers empty for a
	 * player. The two functions are opposite halves of the same question and
	 * neither answers for the other's subject.
	 *
	 * THE FIRST CARRIED TYPE, SINCE ISSUE #837 LAYER 2, when a character carries several: the first in fill order, which
	 * is the creation type whenever it is carried. A reader asking "is this the character's own type" asks
	 * `CarriesDamageType`, and one asking for the character's skills of its types asks `DamageTypesOf`: ruled
	 * 2026-10-25 (Q8), "own type" means any carried type.
	 */
	static FString DamageTypeOf(const AActor* Actor);

	/**
	 * Every Cataclysm damage type an actor carries, in fill order, or none when it has no weapon slots. Issue #837
	 * layer 2. An enemy answers none, as `DamageTypeOf` answers it empty and for the same reason.
	 */
	static TArray<FString> DamageTypesOf(const AActor* Actor);

	/**
	 * Whether an actor carries this damage type: its "own" type, ruled 2026-10-25 (Q8) to mean any type it carries.
	 * False for an actor with no weapon slots, and for an empty or none type. The case ignored.
	 */
	static bool CarriesDamageType(const AActor* Actor, FName DamageType);

	/**
	 * The carried types in fill order: the creation type first when it is carried, then the others in their own order,
	 * which is `UCataclysmEquipmentComponent::CarriedDamageTypes`' order, the first worn weapon's first. Ruled
	 * 2026-10-25 (Q11): "the default fill order is the creation type first, then Weapon1's types, then Weapon2's", with
	 * Weapon1 read as the first OCCUPIED weapon slot. A creation type not carried is not added.
	 */
	static TArray<FString> InFillOrder(const TArray<FName>& Carried, FName CreationDamageType);

	/**
	 * Uses this matrix instead of the generated one.
	 *
	 * For tests, which build a table they control so a case can be set up that
	 * the real matrix does not contain.
	 */
	void SetWeaponSkillTable(const UDataTable* Table) { WeaponSkillTable = Table; }

	/**
	 * Which damage type's skills to use. For tests; see the field below.
	 *
	 * ONE TYPE, AND IT HOLDS, since issue #837 layer 2: the list becomes `{NewDamageType}` (empty for an empty name) and
	 * `UseDamageTypes` leaves it alone afterwards, so a test that pins a type keeps it through any change of gear, as it
	 * did when the type was one field nothing else wrote. Nothing outside the tests calls this any more.
	 */
	void SetDamageType(const FString& NewDamageType)
	{
		DamageTypes.Reset();
		if (!NewDamageType.IsEmpty())
		{
			DamageTypes.Add(NewDamageType);
		}
		bDamageTypesPinned = true;
	}

	/**
	 * The damage types the worn weapons carry, in fill order, for the next fill. Issue #837 layer 2. What the pawn
	 * hands over before it fills the slots. An empty list changes nothing, and neither does any list once a test has
	 * pinned a type with `SetDamageType`.
	 */
	void UseDamageTypes(const TArray<FString>& NewDamageTypes)
	{
		if (!bDamageTypesPinned && !NewDamageTypes.IsEmpty())
		{
			DamageTypes = NewDamageTypes;
		}
	}

	/**
	 * The player's chosen skills, for the next fill. Issue #837 layer 3. What the pawn hands over from
	 * `ACataclysmPlayerState::GetSkillAssignment` before it fills the slots, as it hands the damage types; the fill reads
	 * them through `UCataclysmWeaponSkills::FillWithChoices`. An empty list is the default fill. A component no pawn
	 * hands anything -- a bare actor in a test -- keeps whatever it was handed last, empty at first.
	 */
	void UseSkillAssignment(const TArray<FCataclysmSkillChoice>& NewChoices) { SkillChoices = NewChoices; }

	/** The choices the last fill was handed. */
	const TArray<FCataclysmSkillChoice>& GetSkillChoices() const { return SkillChoices; }

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
	 * Which damage types' skills to use, in fill order.
	 *
	 * THE WORN WEAPONS' TYPES SINCE ISSUE #837 LAYER 2. A weapon's damage types are
	 * rolled when the item drops (#876), and the pawn hands the ones its worn
	 * weapons carry over before every fill (`UseDamageTypes`), the creation type
	 * first when it is carried. What is left of the stand-in is the default below,
	 * for a component no pawn hands anything: a bare actor in a test, or a pawn
	 * before its first fill.
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
	 *
	 * Set to `{Demonic}` in the constructor.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cataclysm|Weapon")
	TArray<FString> DamageTypes;

	/** Whether a test pinned the types with `SetDamageType`, so the worn weapons' are not handed over. */
	bool bDamageTypesPinned = false;

	/**
	 * Which weapon type a character begins holding.
	 *
	 * TEMPORARY, IN THE SAME WAY DamageTypes' DEFAULT ABOVE IS, and it stands in
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

	/**
	 * Everything EquipWeaponTypes does, apart from writing the log line.
	 *
	 * SPLIT OUT SO THE LOG LINE CANNOT BE MISSED. This function leaves by four
	 * different early returns and a fifth is easy to add. EquipWeaponTypes wraps
	 * it and writes exactly one Log-level line whichever way this one leaves, so
	 * an equip that says nothing to the log is not something a later change can
	 * reintroduce by accident.
	 *
	 * That is worth the extra function because it is exactly the fault being
	 * fixed: every line describing an equip was Verbose, none reached the log
	 * file, and a play report about weapons and abilities could not be checked
	 * against the log at all. Issue #1221.
	 *
	 * @return how many slots were filled
	 */
	int32 FillSlotsFromWeaponTypes(const TArray<FString>& NewWeaponTypes);

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

	/** Every worn weapon type the last fill was handed, in slot order. Issue #837 layer 2. */
	UPROPERTY() TArray<FString> WornWeaponTypes;

	/** The whole pool the last fill drew from. Issue #837 layer 2. */
	UPROPERTY() TArray<FCataclysmWeaponSkill> SkillPool;

	/** The player's choices the fills read, as `UseSkillAssignment` was handed them. Issue #837 layer 3. */
	UPROPERTY() TArray<FCataclysmSkillChoice> SkillChoices;

	/** What the current weapon granted, so it can all be taken back. */
	FCataclysmAbilitySetHandles GrantedHandles;
};
