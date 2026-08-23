// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmSkillTemplate.h"
#include "AbilitySystem/CataclysmUndesignedSkill.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Data/CataclysmDataRows.h"
#include "GameplayTagsManager.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"

/**
 * Tests for the equipped weapon deciding what fills the six ability slots.
 *
 * WHAT THESE COVER. That the weapon skill matrix is read correctly, that a
 * weapon its damage type does not cover yields nothing rather than failing, and
 * the acceptance criterion of issue #36: equipping a different weapon type
 * changes every granted ability, with no code naming a skill.
 *
 * WHAT THEY DO NOT COVER. What the abilities DO, because the 61 designed skills
 * carry a name, a description and tags but no numbers, so there is no behaviour
 * to test. Also not the aura's toggle and drain, and not cooldowns on a HUD,
 * neither of which exists.
 */

namespace CataclysmWeaponSlotsTest
{
	/** The damage type whose skills are designed. All 61 of them are War. */
	const TCHAR* DesignedDamageType = TEXT("War");

	/**
	 * Weapon types the design document says War covers. Written down here on
	 * purpose rather than read from the same table under test, so that a table
	 * that lost rows fails rather than agreeing with itself.
	 */
	const TCHAR* WarWeapons[] = {
		TEXT("Sword"), TEXT("Greatsword"), TEXT("Dagger"), TEXT("Axe"),
		TEXT("Greataxe"), TEXT("Spear"), TEXT("Fist"), TEXT("Shield"),
		TEXT("Crossbow"), TEXT("2H Crossbow"), TEXT("Warhammer"), TEXT("Whip"),
	};

	/** An actor carrying an ability system and the component under test. */
	struct FScopedWeaponFixture
	{
		explicit FScopedWeaponFixture(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			check(Actor);

			AbilitySystem = NewObject<UCataclysmAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);

			Slots = NewObject<UCataclysmWeaponSlotsComponent>(Actor);
			Slots->RegisterComponent();

			// Pinned rather than left to the shipping default, because these
			// tests are about the MECHANISM -- that changing weapon replaces
			// every ability -- and not about which damage type the vertical
			// slice happens to ship. War is used because all twelve of its
			// weapons are designed, so any two of them can be swapped. The
			// shipping default is checked separately, by
			// Cataclysm.WeaponSlots.TheSliceShipsDemonic.
			Slots->SetDamageType(DesignedDamageType);
		}

		~FScopedWeaponFixture()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
		}

		/** Which slots currently hold a granted ability, by their Slot.* tag. */
		TSet<FGameplayTag> GrantedSlotTags() const
		{
			TSet<FGameplayTag> Tags;
			for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
			{
				for (const FGameplayTag& Tag : Spec.GetDynamicSpecSourceTags())
				{
					Tags.Add(Tag);
				}
			}
			return Tags;
		}

		int32 GrantedCount() const
		{
			return AbilitySystem->GetActivatableAbilities().Num();
		}

		TObjectPtr<AActor> Actor = nullptr;
		TObjectPtr<UCataclysmAbilitySystemComponent> AbilitySystem = nullptr;
		TObjectPtr<UCataclysmWeaponSlotsComponent> Slots = nullptr;
	};

	UWorld* MakeWorld()
	{
		return UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	}
}

/**
 * Every weapon the design says War covers offers exactly the six slots.
 *
 * Six and not five: the matrix holds five weapon-specific skills per weapon and
 * one Aura skill on a weapon-independent row, because the design gives a damage
 * type one aura rather than one per weapon.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponSkillMatrixTest,
	"Cataclysm.WeaponSlots.EveryWarWeaponOffersAllSixSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeaponSkillMatrixTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!Table)
	{
		AddError(TEXT("Could not load the weapon skill matrix. Run "
					  "tools/generate_datatables.py."));
		return false;
	}

	for (const TCHAR* Weapon : CataclysmWeaponSlotsTest::WarWeapons)
	{
		const TArray<FCataclysmWeaponSkill> Skills = UCataclysmWeaponSkills::SkillsFor(
			Table, Weapon, CataclysmWeaponSlotsTest::DesignedDamageType);

		TestEqual(FString::Printf(TEXT("the %s offers six skills"), Weapon),
			Skills.Num(), 6);

		// One per slot, and never two claiming the same one. Two abilities
		// carrying the same slot tag would mean one key firing both.
		TSet<ECataclysmAbilitySlot> Slots;
		for (const FCataclysmWeaponSkill& Skill : Skills)
		{
			TestNotEqual(FString::Printf(TEXT("%s: %s names a real slot"),
				Weapon, *Skill.Name), Skill.Slot, ECataclysmAbilitySlot::None);
			TestFalse(FString::Printf(TEXT("%s: nothing claims a slot twice"), Weapon),
				Slots.Contains(Skill.Slot));
			TestFalse(FString::Printf(TEXT("%s: every skill is named"), Weapon),
				Skill.Name.IsEmpty());
			Slots.Add(Skill.Slot);
		}
	}

	return true;
}

/**
 * A weapon its damage type does not cover offers nothing, and that is correct.
 *
 * The design document gives each damage type its own list of weapon types. War
 * covers twelve and includes neither the Wand nor the Staff. Asserting the empty
 * result rather than skipping it is what stops someone "fixing" it later by
 * giving War magic weapons it is not meant to have.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponSkillAbsenceTest,
	"Cataclysm.WeaponSlots.AWeaponItsDamageTypeDoesNotCoverOffersNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeaponSkillAbsenceTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!Table)
	{
		AddError(TEXT("Could not load the weapon skill matrix."));
		return false;
	}

	for (const TCHAR* Magic : {TEXT("Wand"), TEXT("Staff")})
	{
		TestEqual(FString::Printf(TEXT("War has no %s skills, which is the design"), Magic),
			UCataclysmWeaponSkills::SkillsFor(
				Table, Magic, CataclysmWeaponSlotsTest::DesignedDamageType).Num(), 0);
	}

	// A weapon type that does not exist at all behaves the same way rather than
	// crashing or matching something by accident.
	TestEqual(TEXT("an unknown weapon type offers nothing"),
		UCataclysmWeaponSkills::SkillsFor(
			Table, TEXT("Banana"), CataclysmWeaponSlotsTest::DesignedDamageType).Num(), 0);

	return true;
}

/**
 * THE ACCEPTANCE CRITERION OF ISSUE #36. Equipping a different weapon type
 * changes all six abilities, and nothing in C++ names a skill to make it happen.
 *
 * Checked as: six slots filled, then a different weapon still fills six, and the
 * skills behind them are entirely different. Also that the previous weapon's
 * abilities are gone rather than sitting alongside the new ones, because two
 * abilities carrying the same slot tag means one key firing both.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponSwapTest,
	"Cataclysm.WeaponSlots.ChangingWeaponTypeReplacesEveryAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeaponSwapTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmWeaponSlotsTest::MakeWorld();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	CataclysmWeaponSlotsTest::FScopedWeaponFixture Fixture(World);

	TestEqual(TEXT("nothing is granted before a weapon is equipped"),
		Fixture.GrantedCount(), 0);

	const int32 DaggerFilled = Fixture.Slots->EquipWeaponType(TEXT("Dagger"));
	TestEqual(TEXT("a Dagger fills seven slots"), DaggerFilled, 7);
	TestEqual(TEXT("and grants exactly seven abilities"), Fixture.GrantedCount(), 7);

	const TSet<FGameplayTag> DaggerTags = Fixture.GrantedSlotTags();
	TestEqual(TEXT("seven distinct slot tags, so no key fires two abilities"),
		DaggerTags.Num(), 7);

	TArray<FString> DaggerSkills;
	for (const FCataclysmWeaponSkill& Skill : Fixture.Slots->GetAvailableSkills())
	{
		DaggerSkills.Add(Skill.Name);
	}

	// A different weapon type. No code below names a skill; the change comes
	// entirely from the matrix.
	const int32 WarhammerFilled = Fixture.Slots->EquipWeaponType(TEXT("Warhammer"));
	TestEqual(TEXT("a Warhammer also fills seven slots"), WarhammerFilled, 7);

	TestEqual(TEXT("and the Dagger's abilities are gone, not still granted"),
		Fixture.GrantedCount(), 7);

	TestEqual(TEXT("still seven distinct slot tags"),
		Fixture.GrantedSlotTags().Num(), 7);

	// The same seven slots are filled, by different skills. That is what makes a
	// weapon drop change how the character plays rather than only its numbers.
	TestTrue(TEXT("the same seven slots are filled"),
		Fixture.GrantedSlotTags().Includes(DaggerTags));

	int32 Shared = 0;
	for (const FCataclysmWeaponSkill& Skill : Fixture.Slots->GetAvailableSkills())
	{
		if (DaggerSkills.Contains(Skill.Name))
		{
			++Shared;
		}
	}

	// TWO SHARED, AND THEY ARE SHARED FOR DIFFERENT REASONS. The Aura is
	// weapon-independent, so both weapons offer the same one. The basic attack is
	// generic rather than designed per weapon, so both carry the same name while
	// swinging at different reaches -- the Dagger's 1.5 metres and the
	// Warhammer's 2.1. Issue #524. If this were seven, equipping a different
	// weapon would have changed nothing.
	TestEqual(TEXT("the Aura and the basic attack are shared, and nothing else"),
		Shared, 2);

	Fixture.Slots->UnequipWeapon();
	TestEqual(TEXT("unequipping takes every ability back"), Fixture.GrantedCount(), 0);

	return true;
}

/**
 * A weapon with no matrix skills keeps only its basic attack and leaves nothing
 * else behind.
 *
 * The dangerous shape is equipping a covered weapon and then an uncovered one:
 * if unequipping did not happen first, the character would keep the old
 * weapon's abilities while holding a weapon that offers none.
 *
 * IT KEEPS ONE RATHER THAN NONE, SINCE ISSUE #524. The basic attack comes from
 * the weapon's own base row and not from the matrix, so a War Wand -- a
 * combination the design says has no skills -- still swings. What must not
 * survive is any of the Sword's six.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponWithoutSkillsTest,
	"Cataclysm.WeaponSlots.EquippingAWeaponWithNoSkillsClearsTheOldOnes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeaponWithoutSkillsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmWeaponSlotsTest::MakeWorld();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	CataclysmWeaponSlotsTest::FScopedWeaponFixture Fixture(World);

	Fixture.Slots->EquipWeaponType(TEXT("Sword"));
	TestEqual(TEXT("a Sword fills seven slots"), Fixture.GrantedCount(), 7);

	// War covers no magic weapons, so the matrix gives this nothing.
	const int32 Filled = Fixture.Slots->EquipWeaponType(TEXT("Wand"));
	TestEqual(TEXT("a War Wand fills only its basic attack"), Filled, 1);
	TestEqual(TEXT("and the Sword's seven went with it"),
		Fixture.GrantedCount(), 1);

	const TArray<FCataclysmWeaponSkill>& Wand = Fixture.Slots->GetAvailableSkills();
	TestEqual(TEXT("it reports exactly one available skill"), Wand.Num(), 1);
	if (Wand.Num() == 1)
	{
		TestEqual(TEXT("and that one is the basic attack"),
			Wand[0].Slot, ECataclysmAbilitySlot::BasicAttack);
	}

	// The Shield is the one weapon that arms nobody, so it is the one that still
	// fills nothing at all. Without this the check above would pass for a change
	// that granted a basic attack to everything holdable.
	const int32 ShieldFilled = Fixture.Slots->EquipWeaponType(TEXT("Shield"));
	TestEqual(TEXT("a War Shield fills its six matrix slots and no basic attack"),
		ShieldFilled, 6);
	for (const FCataclysmWeaponSkill& Skill : Fixture.Slots->GetAvailableSkills())
	{
		TestNotEqual(TEXT("no Shield skill sits in the basic attack slot"),
			Skill.Slot, ECataclysmAbilitySlot::BasicAttack);
	}

	return true;
}

/**
 * A granted ability sits in the slot the WEAPON chose, not the one its class
 * declares.
 *
 * UCataclysmUndesignedSkill declares no slot, because one class stands in for
 * every undesigned skill across all six. If granting read the class's slot
 * instead of the caller's, every ability would land in None, carry no tag, and
 * no key would reach any of them -- and nothing would report it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSlotComesFromTheWeaponTest,
	"Cataclysm.WeaponSlots.TheSlotComesFromTheWeaponNotTheAbilityClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSlotComesFromTheWeaponTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("the stand-in ability class declares no slot of its own"),
		GetDefault<UCataclysmUndesignedSkill>()->Slot, ECataclysmAbilitySlot::None);

	UWorld* World = CataclysmWeaponSlotsTest::MakeWorld();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	CataclysmWeaponSlotsTest::FScopedWeaponFixture Fixture(World);
	Fixture.Slots->EquipWeaponType(TEXT("Axe"));

	// Every slot the matrix said the Axe fills has a granted ability carrying
	// that slot's tag.
	for (const FCataclysmWeaponSkill& Skill : Fixture.Slots->GetAvailableSkills())
	{
		const FGameplayTag Expected = CataclysmAbilitySlots::Tag(Skill.Slot);
		TestTrue(FString::Printf(TEXT("%s was granted into its own slot"), *Skill.Name),
			Fixture.GrantedSlotTags().Contains(Expected));
	}

	// And granting into no slot at all is refused rather than producing an
	// ability no key can reach.
	const FGameplayAbilitySpecHandle Refused = Fixture.AbilitySystem->GiveAbilityInSlot(
		UCataclysmUndesignedSkill::StaticClass(), ECataclysmAbilitySlot::None);
	TestFalse(TEXT("granting into no slot is refused"), Refused.IsValid());

	return true;
}

/**
 * The vertical slice ships Demonic, and its three weapons fill every slot.
 *
 * ISSUE #61 SETTLED THIS: the Cataclysm being fought decides the player's damage
 * type, and the design document's Phase 1 roadmap names the Demonic Cataclysm,
 * the Demonic Masochist tree and Demonic skills across three weapon types.
 * Shipping War would drop loot the slice's player content cannot use.
 *
 * The fixture above pins War deliberately, because those tests are about the
 * swap mechanism. This one checks the shipping default, which nothing else does.
 */
/**
 * The weapon a character begins holding must actually grant them skills.
 *
 * WHAT THIS CATCHES, AND IT IS THE FAILURE THAT PRODUCED ISSUE #169. A starting
 * weapon type the shipping damage type does not cover -- a Crossbow while the
 * slice ships Demonic, or a misspelling -- fills no slot at all. The game still
 * runs, the character still walks, and every skill key does nothing. There is no
 * error and nothing looks wrong.
 *
 * Deliberately reads the SHIPPING defaults for both the starting weapon type and
 * the damage type, and sets neither, because the pairing is what matters and
 * either one being changed alone can break it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStartingWeaponGrantsSkillsTest,
	"Cataclysm.WeaponSlots.TheStartingWeaponActuallyGrantsSkills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStartingWeaponGrantsSkillsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmWeaponSlotsTest::MakeWorld();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	AActor* Actor = World->SpawnActor<AActor>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		NewObject<UCataclysmAbilitySystemComponent>(Actor);
	AbilitySystem->RegisterComponent();
	AbilitySystem->InitAbilityActorInfo(Actor, Actor);

	UCataclysmWeaponSlotsComponent* Slots =
		NewObject<UCataclysmWeaponSlotsComponent>(Actor);
	Slots->RegisterComponent();

	const FString Starting = Slots->GetStartingWeaponType();
	TestFalse(TEXT("A starting weapon type is set at all"), Starting.IsEmpty());

	const int32 Filled = Slots->EquipStartingWeapon();

	TestEqual(FString::Printf(
		TEXT("Beginning with a %s and damage type %s fills all seven slots"),
		*Starting, *Slots->GetDamageType()), Filled, 7);

	// SIX OF THE SEVEN ARE THE MATRIX'S AND THE SEVENTH IS THE WEAPON'S OWN.
	// Counted separately because the basic attack is granted whatever the damage
	// type covers, so a starting weapon with no designed skills at all would
	// still report one filled slot. That is the case issue #169 was about, and
	// the count alone no longer distinguishes it. Issue #524.
	int32 Designed = 0;
	int32 BasicAttacks = 0;
	for (const FCataclysmWeaponSkill& Skill : Slots->GetAvailableSkills())
	{
		(Skill.Slot == ECataclysmAbilitySlot::BasicAttack ? BasicAttacks : Designed)++;
	}
	TestEqual(TEXT("six of them are designed skills from the matrix"), Designed, 6);
	TestEqual(TEXT("and exactly one is the weapon's own basic attack"),
		BasicAttacks, 1);

	// And it really equipped the type it names, rather than filling seven slots
	// from something else.
	TestEqual(TEXT("The equipped type is the starting type"),
		Slots->GetEquippedWeaponType(), Starting);

	Actor->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBadStartingWeaponGrantsNothingTest,
	"Cataclysm.WeaponSlots.AStartingWeaponTheDamageTypeDoesNotCoverGrantsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBadStartingWeaponGrantsNothingTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmWeaponSlotsTest::MakeWorld();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	AActor* Actor = World->SpawnActor<AActor>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		NewObject<UCataclysmAbilitySystemComponent>(Actor);
	AbilitySystem->RegisterComponent();
	AbilitySystem->InitAbilityActorInfo(Actor, Actor);

	UCataclysmWeaponSlotsComponent* Slots =
		NewObject<UCataclysmWeaponSlotsComponent>(Actor);
	Slots->RegisterComponent();

	// The Crossbow is a real weapon base that Demonic does not roll on, so this
	// is the realistic version of the mistake rather than a nonsense string.
	// It fills its basic attack and nothing else, because that comes from the
	// weapon base rather than from the matrix. Issue #524.
	Slots->SetStartingWeaponType(TEXT("Crossbow"));
	TestEqual(TEXT("Beginning with a Demonic Crossbow fills only a basic attack"),
		Slots->EquipStartingWeapon(), 1);
	for (const FCataclysmWeaponSkill& Skill : Slots->GetAvailableSkills())
	{
		TestEqual(TEXT("and that one slot is the basic attack, not a designed skill"),
			Skill.Slot, ECataclysmAbilitySlot::BasicAttack);
	}

	// And a name that is not a weapon at all does the same, rather than
	// erroring or filling something arbitrary.
	Slots->SetStartingWeaponType(TEXT("Greetaxe"));
	TestEqual(TEXT("A misspelled starting weapon fills no slot"),
		Slots->EquipStartingWeapon(), 0);

	// Naming nothing is a legitimate choice and must not crash.
	Slots->SetStartingWeaponType(FString());
	TestEqual(TEXT("Naming no starting weapon fills no slot"),
		Slots->EquipStartingWeapon(), 0);

	Actor->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSliceShipsDemonicTest,
	"Cataclysm.WeaponSlots.TheSliceShipsDemonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSliceShipsDemonicTest::RunTest(const FString& Parameters)
{
	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!Table)
	{
		AddError(TEXT("DT_WeaponSkills does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	UWorld* World = CataclysmWeaponSlotsTest::MakeWorld();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	// Deliberately does NOT call SetDamageType, so this reads whatever the
	// component ships with. That is the whole point of the test.
	AActor* Actor = World->SpawnActor<AActor>();
	UCataclysmAbilitySystemComponent* AbilitySystem =
		NewObject<UCataclysmAbilitySystemComponent>(Actor);
	AbilitySystem->RegisterComponent();
	AbilitySystem->InitAbilityActorInfo(Actor, Actor);

	UCataclysmWeaponSlotsComponent* Slots =
		NewObject<UCataclysmWeaponSlotsComponent>(Actor);
	Slots->RegisterComponent();

	// One weapon per Demonic class: Greataxe for the Ravager, Fist for the
	// Masochist, Staff for the Ritualist. Named here rather than read from the
	// matrix, because the point is that these three specifically are the slice.
	const TCHAR* SliceWeapons[] = { TEXT("Greataxe"), TEXT("Fist"), TEXT("Staff") };
	for (const TCHAR* Weapon : SliceWeapons)
	{
		const int32 Filled = Slots->EquipWeaponType(Weapon);
		TestEqual(FString::Printf(
			TEXT("a Demonic %s fills all seven slots"), Weapon), Filled, 7);
	}

	// ALL TEN OF DEMONIC'S WEAPONS ARE NOW DESIGNED. This test used to assert the
	// opposite -- that a Demonic Dagger granted nothing, because seven of the ten
	// were still empty. The 35 missing rows were filled, so the assertion is
	// inverted rather than deleted: the thing worth holding is that every weapon
	// the design says Demonic covers actually fills its slots.
	const TCHAR* OtherDemonicWeapons[] = {
		TEXT("Sword"), TEXT("Greatsword"), TEXT("Dagger"), TEXT("Axe"),
		TEXT("Wand"), TEXT("Whip"), TEXT("Warhammer"),
	};
	for (const TCHAR* Weapon : OtherDemonicWeapons)
	{
		const int32 Filled = Slots->EquipWeaponType(Weapon);
		TestEqual(FString::Printf(
			TEXT("a Demonic %s fills all seven slots"), Weapon), Filled, 7);
	}

	// A weapon type Demonic does not cover grants only its basic attack, which is
	// what keeps the check above from passing for any string at all. It was zero
	// before issue #524, because the basic attack did not exist.
	const int32 NotCovered = Slots->EquipWeaponType(TEXT("Crossbow"));
	TestEqual(TEXT("Demonic does not cover the Crossbow, so it grants only a "
				   "basic attack"),
		NotCovered, 1);

	// And a name that is no weapon at all still grants nothing, so the line above
	// is not merely reporting that everything gets one.
	const int32 NotAWeapon = Slots->EquipWeaponType(TEXT("Greetaxe"));
	TestEqual(TEXT("a misspelled weapon type grants nothing at all"),
		NotAWeapon, 0);

	Actor->Destroy();
	return true;
}

/**
 * Every weapon that arms its holder grants a basic attack, and it carries the
 * reach written on that weapon's own base row.
 *
 * WHAT WENT WRONG. Issue #524: game/Data/WeaponSkills.csv held 398 rows across
 * six slots and not one Basic row, so no character had an ordinary attack at all.
 * The project owner found it by playing -- two abilities and nothing in between.
 *
 * WHY IT IS READ FROM ItemBases RATHER THAN FROM THE MATRIX. The basic attack is
 * weapon damage itself, so it does not vary by damage type. One entry per weapon
 * rather than one per weapon-and-damage-type pair, and the matrix keeps holding
 * one skill per non-basic slot exactly as the design document states.
 *
 * THE NUMBERS ARE NOT REPEATED HERE. Asserting 1.5 metres for a Dagger in C++
 * would only prove the CSV was copied twice. What is checked is that the granted
 * ability's parameters are the ones on the base row, that no two weapons swing
 * the same distance, and that every armed weapon has one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackTest,
	"Cataclysm.WeaponSlots.EveryArmedWeaponGrantsABasicAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackTest::RunTest(const FString& Parameters)
{
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!Bases)
	{
		AddError(TEXT("DT_ItemBases does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	// Read from the table rather than written here, so a weapon added to the
	// workbook is covered without editing this test.
	TArray<FString> Armed;
	TArray<FString> Unarmed;
	Bases->ForeachRow<FCataclysmItemBaseRow>(TEXT("FCataclysmBasicAttackTest"),
		[&](const FName&, const FCataclysmItemBaseRow& Row)
		{
			if (Row.WeaponType.IsEmpty())
			{
				return;
			}
			(Row.BasicShape.IsEmpty() ? Unarmed : Armed).AddUnique(Row.WeaponType);
		});

	TestEqual(TEXT("thirteen weapons state a basic attack"), Armed.Num(), 13);
	TestEqual(TEXT("and one states none, which is the Shield"), Unarmed.Num(), 1);
	if (Unarmed.Num() == 1)
	{
		TestEqual(TEXT("the weapon with no basic attack is the Shield"),
			Unarmed[0], FString(TEXT("Shield")));
	}

	// Every armed weapon's basic attack is found, has a shape a template
	// implements, and states a reach. A shape with no template would fill the
	// slot and do nothing, which is the failure this catches.
	TSet<float> Reaches;
	for (const FString& Weapon : Armed)
	{
		const FCataclysmWeaponSkill Basic =
			UCataclysmWeaponSkills::BasicAttackFor(Bases, Weapon);

		TestEqual(FString::Printf(TEXT("the %s's basic attack fills the Basic slot"),
			*Weapon), Basic.Slot, ECataclysmAbilitySlot::BasicAttack);
		TestNotEqual(FString::Printf(TEXT("the %s's basic attack has a real shape"),
			*Weapon), Basic.Shape, ECataclysmSkillShape::None);
		TestTrue(FString::Printf(TEXT("a template runs the %s's basic attack"),
			*Weapon), UCataclysmWeaponSkills::TemplateFor(Basic.Shape) != nullptr);

		// A Strike states how far it reaches as a Radius and a Projectile as a
		// Range, so whichever the shape uses must be there and above zero. A
		// reach of zero hits nothing and looks exactly like a reach nobody wrote.
		const bool bMelee = Basic.Shape == ECataclysmSkillShape::Strike;
		const float Reach = bMelee ? Basic.Params.RadiusCm : Basic.Params.RangeCm;
		TestTrue(FString::Printf(
			TEXT("the %s's basic attack reaches further than nothing, got %.2f cm"),
			*Weapon, Reach), Reach > 0.0f);

		Reaches.Add(Reach);
	}

	// SEVERAL WEAPONS SHARE A REACH ON PURPOSE -- the Dagger and the Fist are both
	// 1.5 metres -- so this is not one distinct value per weapon. What it refuses
	// is every weapon reading the same number, which is what a lookup that always
	// found the first row would produce.
	TestTrue(FString::Printf(
		TEXT("weapons swing different distances, found %d distinct reaches"),
		Reaches.Num()), Reaches.Num() >= 8);

	// The Shield asks and gets nothing, rather than getting a shapeless skill
	// that fills the slot.
	const FCataclysmWeaponSkill None =
		UCataclysmWeaponSkills::BasicAttackFor(Bases, TEXT("Shield"));
	TestEqual(TEXT("the Shield's basic attack fills no slot"),
		None.Slot, ECataclysmAbilitySlot::None);

	// A weapon type that does not exist does the same, rather than returning the
	// first row it read.
	const FCataclysmWeaponSkill Misspelled =
		UCataclysmWeaponSkills::BasicAttackFor(Bases, TEXT("Greetaxe"));
	TestEqual(TEXT("a misspelled weapon type gets no basic attack"),
		Misspelled.Slot, ECataclysmAbilitySlot::None);

	// And a null table is survivable, because LoadBaseTable can fail.
	const FCataclysmWeaponSkill NoTable =
		UCataclysmWeaponSkills::BasicAttackFor(nullptr, TEXT("Dagger"));
	TestEqual(TEXT("no table means no basic attack rather than a crash"),
		NoTable.Slot, ECataclysmAbilitySlot::None);

	return true;
}

/**
 * The basic attack carries the tags that let gear modifiers reach it.
 *
 * WHAT THIS CATCHES, AND IT IS INVISIBLE WITHOUT A TEST. Tags decide which of a
 * character's stat modifiers apply to a skill:
 * UCataclysmStatPipeline::ModifierApplies asks whether the skill in hand carries
 * every tag a modifier requires, and returns false silently when it does not. A
 * basic attack with an empty tag container is granted, fires, deals damage, and
 * is reached by no scoped modifier at all -- and nothing reports it.
 *
 * That would matter more than for any other slot. The basic attack is 100%
 * weapon damage, which is what makes it the anchor the other six slots are
 * percentages of, so a buff that increased six of the seven would move the ratio
 * between them without anything failing.
 *
 * TWO TAGS, FROM TWO PLACES, WHICH IS WHY BOTH ARE CHECKED. The slot tag is
 * invariant and comes from UCataclysmWeaponSkills::BasicAttackFor. The element
 * tag is the weapon's rolled damage type, which one Item Bases row cannot state
 * because that row serves every damage type the weapon can roll, so
 * UCataclysmWeaponSlotsComponent adds it at equip time.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackCarriesItsTagsTest,
	"Cataclysm.WeaponSlots.TheBasicAttackCarriesItsSlotAndElementTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackCarriesItsTagsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmWeaponSlotsTest::MakeWorld();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	CataclysmWeaponSlotsTest::FScopedWeaponFixture Fixture(World);

	const FGameplayTag SlotTag =
		CataclysmAbilitySlots::Tag(ECataclysmAbilitySlot::BasicAttack);
	TestTrue(TEXT("Slot.Basic is a registered tag"), SlotTag.IsValid());

	// The fixture pins War, so this is the tag the equipped weapon should carry.
	const FGameplayTag ElementTag = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Element.War")), /*ErrorIfNotFound=*/false);
	TestTrue(TEXT("Element.War is a registered tag"), ElementTag.IsValid());

	Fixture.Slots->EquipWeaponType(TEXT("Dagger"));

	const FCataclysmWeaponSkill* Basic = nullptr;
	for (const FCataclysmWeaponSkill& Skill : Fixture.Slots->GetAvailableSkills())
	{
		if (Skill.Slot == ECataclysmAbilitySlot::BasicAttack)
		{
			Basic = &Skill;
			break;
		}
	}

	if (!Basic)
	{
		AddError(TEXT("A War Dagger granted no basic attack at all."));
		return false;
	}

	TestTrue(TEXT("the basic attack carries its own slot tag"),
		Basic->Tags.HasTagExact(SlotTag));
	TestTrue(TEXT("and the equipped weapon's element tag"),
		Basic->Tags.HasTagExact(ElementTag));

	// THE TAGS HAVE TO REACH THE GRANTED INSTANCE, not merely sit on the struct.
	// UCataclysmStatPipeline reads them off the ability, so a component that
	// collected them and did not copy them across would fail exactly the same way
	// as one that never collected them.
	bool bFoundInstance = false;
	for (const FGameplayAbilitySpec& Spec :
			Fixture.AbilitySystem->GetActivatableAbilities())
	{
		const UCataclysmSkillTemplate* Template =
			Cast<UCataclysmSkillTemplate>(Spec.GetPrimaryInstance());
		if (!Template || !Template->SkillTags.HasTagExact(SlotTag))
		{
			continue;
		}
		bFoundInstance = true;
		TestTrue(TEXT("the granted instance carries the element tag too"),
			Template->SkillTags.HasTagExact(ElementTag));
	}
	TestTrue(TEXT("a granted ability instance carries the basic attack slot tag"),
		bFoundInstance);

	// AND THE ELEMENT FOLLOWS THE WEAPON'S DAMAGE TYPE RATHER THAN BEING FIXED.
	// Without this the checks above would pass for a change that hard-coded one
	// element, which is the mistake worth guarding against: the same Item Bases
	// row serves a Dagger of every damage type.
	Fixture.Slots->SetDamageType(TEXT("Demonic"));
	Fixture.Slots->EquipWeaponType(TEXT("Dagger"));

	const FGameplayTag Demonic = UGameplayTagsManager::Get().RequestGameplayTag(
		FName(TEXT("Element.Demonic")), /*ErrorIfNotFound=*/false);

	for (const FCataclysmWeaponSkill& Skill : Fixture.Slots->GetAvailableSkills())
	{
		if (Skill.Slot != ECataclysmAbilitySlot::BasicAttack)
		{
			continue;
		}
		TestTrue(TEXT("a Demonic Dagger's basic attack carries Element.Demonic"),
			Skill.Tags.HasTagExact(Demonic));
		TestFalse(TEXT("and no longer carries Element.War"),
			Skill.Tags.HasTagExact(ElementTag));
	}

	return true;
}

/**
 * The granted ability carries the base row's own numbers, not a default.
 *
 * The failure this catches is a basic attack that is granted into its slot and
 * reaches zero, which looks identical to a working one from every other angle.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmBasicAttackReachesWhatTheBaseSaysTest,
	"Cataclysm.WeaponSlots.TheBasicAttackCarriesTheWeaponsOwnReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmBasicAttackReachesWhatTheBaseSaysTest::RunTest(const FString& Parameters)
{
	UWorld* World = CataclysmWeaponSlotsTest::MakeWorld();
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!Bases)
	{
		AddError(TEXT("DT_ItemBases does not exist. Run "
					  "tools/generate_datatable_assets.py."));
		return false;
	}

	CataclysmWeaponSlotsTest::FScopedWeaponFixture Fixture(World);

	// A melee weapon and a ranged one, so both shapes are covered. Both are War,
	// which the fixture pins.
	const TCHAR* Weapons[] = { TEXT("Dagger"), TEXT("Crossbow") };
	for (const TCHAR* Weapon : Weapons)
	{
		Fixture.Slots->EquipWeaponType(Weapon);

		const FCataclysmWeaponSkill* Granted = nullptr;
		for (const FCataclysmWeaponSkill& Skill : Fixture.Slots->GetAvailableSkills())
		{
			if (Skill.Slot == ECataclysmAbilitySlot::BasicAttack)
			{
				Granted = &Skill;
				break;
			}
		}

		if (!Granted)
		{
			AddError(FString::Printf(
				TEXT("the %s granted no basic attack at all"), Weapon));
			continue;
		}

		const FCataclysmWeaponSkill FromBase =
			UCataclysmWeaponSkills::BasicAttackFor(Bases, Weapon);

		TestEqual(FString::Printf(
			TEXT("the granted %s basic attack has the base row's shape"), Weapon),
			Granted->Shape, FromBase.Shape);

		const bool bMelee = FromBase.Shape == ECataclysmSkillShape::Strike;
		const float GrantedReach =
			bMelee ? Granted->Params.RadiusCm : Granted->Params.RangeCm;
		const float BaseReach =
			bMelee ? FromBase.Params.RadiusCm : FromBase.Params.RangeCm;

		TestTrue(FString::Printf(
			TEXT("the %s's basic attack reaches further than nothing, got %.2f cm"),
			Weapon, BaseReach), BaseReach > 0.0f);
		TestEqual(FString::Printf(
			TEXT("the granted %s basic attack reaches what the base row says"),
			Weapon), GrantedReach, BaseReach);

		// NOTHING ELSE RIDES ON IT. A basic attack is 100% weapon damage, which
		// is what makes it the anchor every other slot is a percentage of, so a
		// burn or a patch of ground here would move that anchor. The generator
		// refuses one; this confirms none arrived by another route.
		TestFalse(FString::Printf(
			TEXT("the %s's basic attack sets nothing alight"), Weapon),
			Granted->Params.bBurns);
		TestFalse(FString::Printf(
			TEXT("the %s's basic attack leaves no ground behind"), Weapon),
			Granted->Params.LeavesGround());
		TestEqual(FString::Printf(
			TEXT("the %s's basic attack knocks nothing back"), Weapon),
			Granted->Params.KnockbackCm, 0.0f);
		TestEqual(FString::Printf(
			TEXT("the %s's basic attack applies no status effect"), Weapon),
			Granted->Params.Effect, FString());
	}

	return true;
}

/**
 * Which sub-type the equipped weapon gives a hit. Issue #639.
 *
 * NOTHING JOINED THE WEAPON TO A HIT BEFORE THIS. `FCataclysmIncomingHit`
 * carries `bIsSlashing`, `bIsMagic` and now `bIsPiercing`, and
 * `UCataclysmDamageCalculation::Resolve` applies all three correctly. None was
 * ever set, so slashing, magic and piercing all did nothing whatever weapon was
 * held.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCataclysmWeaponSubTypeTest,
	"Cataclysm.WeaponSlots.TheEquippedWeaponDecidesTheSubType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeaponSubTypeTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		AddError(TEXT("could not make a world"));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	AActor* Actor = World->SpawnActor<AActor>();
	if (!Actor)
	{
		AddError(TEXT("could not spawn an actor"));
		return false;
	}

	UCataclysmWeaponSlotsComponent* Slots =
		NewObject<UCataclysmWeaponSlotsComponent>(Actor);
	Slots->RegisterComponent();
	Slots->SetDamageType(TEXT("Demonic"));

	// EMPTY BEFORE ANYTHING IS HELD, which is what makes the rest meaningful.
	TestEqual(TEXT("a character holding nothing has no sub-type"),
		Slots->GetEquippedSubType(), FString());

	// EVERY SUB-TYPE THE ITEM BASES SHEET USES, one weapon each, so a lookup
	// that answered the same thing for everything would fail here.
	struct FCase { const TCHAR* Weapon; const TCHAR* SubType; };
	const FCase Cases[] = {
		{ TEXT("Axe"),        TEXT("Slashing") },
		{ TEXT("Dagger"),     TEXT("Piercing") },
		{ TEXT("Wand"),       TEXT("Magic") },
		{ TEXT("Fist"),       TEXT("Blunt") },
		{ TEXT("Greatsword"), TEXT("Slashing") },
		{ TEXT("Warhammer"),  TEXT("Blunt") },
	};

	for (const FCase& Case : Cases)
	{
		Slots->EquipWeaponType(Case.Weapon);
		TestEqual(FString::Printf(TEXT("a %s is %s"), Case.Weapon, Case.SubType),
			Slots->GetEquippedSubType(), FString(Case.SubType));
	}

	// AND PUTTING IT DOWN LEAVES NOTHING BEHIND, so a sub-type cannot outlive
	// the weapon that gave it.
	Slots->UnequipWeapon();
	TestEqual(TEXT("unequipping clears the sub-type"),
		Slots->GetEquippedSubType(), FString());

	// AN ACTOR WITH NO WEAPON SLOTS AT ALL ANSWERS EMPTY, which is every enemy.
	// That is the answer every hit gave before this existed, so nothing about an
	// enemy's hit changes.
	AActor* Bare = World->SpawnActor<AActor>();
	TestEqual(TEXT("an actor with no weapon slots has no sub-type"),
		UCataclysmWeaponSlotsComponent::SubTypeOf(Bare), FString());
	TestEqual(TEXT("and neither does nothing at all"),
		UCataclysmWeaponSlotsComponent::SubTypeOf(nullptr), FString());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmWeaponSlotsWriteTheBaseCritChance,
	"Cataclysm.WeaponSlots.GrantedSkillsBringTheirBaseCriticalStrikeChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmWeaponSlotsWriteTheBaseCritChance::RunTest(const FString&)
{
	// NOTHING WROTE THIS UNTIL ISSUE #649, so a player's critical strike chance
	// stood at the zero the attribute set initialises it to, with the comment
	// "supplied by the skill in use" describing an intention nobody had built.
	// The player never critically struck, and everything that scales the stat --
	// the Ferocity attribute, three affixes, two gems, eight item base implicits
	// and two whole passive tree branches -- multiplied zero.
	//
	// IT IS THE SKILL'S NUMBER AND NOT THE WEAPON'S. The design's stat source
	// table says "the skill being used" supplies critical strike chance, and the
	// sentence after it is explicit: "A character has no critical strike chance
	// in the abstract." It is written here because this is the moment a weapon's
	// skills are granted, and the moment they are taken away again.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game,
									   /*bInformEngineOfWorld=*/false);
	if (!World)
	{
		AddError(TEXT("Could not create a world."));
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(/*bInformEngineOfWorld=*/false); };

	AActor* Actor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("an actor"), Actor))
	{
		return false;
	}

	UCataclysmAbilitySystemComponent* AbilitySystem =
		NewObject<UCataclysmAbilitySystemComponent>(Actor);
	AbilitySystem->RegisterComponent();

	// A raw pointer on purpose: AddAttributeSetSubobject is a template and a
	// TObjectPtr would deduce the wrapper rather than the set.
	UCataclysmCombatAttributeSet* Combat =
		NewObject<UCataclysmCombatAttributeSet>(Actor);
	AbilitySystem->AddAttributeSetSubobject(Combat);
	AbilitySystem->InitAbilityActorInfo(Actor, Actor);

	UCataclysmWeaponSlotsComponent* Slots =
		NewObject<UCataclysmWeaponSlotsComponent>(Actor);
	Slots->RegisterComponent();
	Slots->SetDamageType(CataclysmWeaponSlotsTest::DesignedDamageType);

	TestEqual(TEXT("a character holding nothing has no critical strike chance"),
		Combat->GetCritChance(), 0.0f, 0.001f);

	Slots->EquipWeaponType(TEXT("Sword"));
	TestEqual(TEXT("a weapon's skills bring the 5% base with them"),
		Combat->GetCritChance(),
		UCataclysmWeaponSlotsComponent::DefaultSkillCritChancePercent, 0.001f);

	// 5 RATHER THAN WHATEVER THE CONSTANT SAYS. Comparing the attribute with the
	// constant alone would pass if both were zero, which is the exact state this
	// test exists to catch.
	TestEqual(TEXT("and that base is five percent"),
		UCataclysmWeaponSlotsComponent::DefaultSkillCritChancePercent, 5.0f,
		0.001f);

	// SWAPPING KEEPS IT, because the new weapon grants its own skills and they
	// take the same default.
	Slots->EquipWeaponType(TEXT("Dagger"));
	TestEqual(TEXT("swapping weapon keeps it"), Combat->GetCritChance(), 5.0f,
		0.001f);

	// AND PUTTING THE WEAPON DOWN TAKES IT AWAY, for the same reason the weapon's
	// damage and its rate go: a character holding nothing swings nothing, so
	// there is no skill in use to supply a chance.
	Slots->UnequipWeapon();
	TestEqual(TEXT("and putting it down takes it away again"),
		Combat->GetCritChance(), 0.0f, 0.001f);

	return true;
}

/**
 * A row's own critical strike chance is stamped onto the granted skill. #657.
 *
 * THE LINK BETWEEN THE DATA AND THE RUNNING ABILITY. `Cataclysm.Crit.ASkillRows
 * OwnCriticalStrikeChanceReachesTheSkill` proves a row becomes a skill carrying
 * the figure, and `Cataclysm.Skills.ASkillsOwnCriticalStrikeChanceReachesWhat
 * ItHits` proves a skill carrying it uses it. This is the step between: the
 * figure being written onto the ability the character is actually granted.
 *
 * ONTO THE INSTANCE AND NOT ONTO THE CHARACTER, which is the point. Six skills
 * are granted at once and the ability system has one `CritChance` attribute, so
 * writing it there would let the last skill granted decide for all six. This test
 * grants two skills whose rows differ and checks they still differ afterwards,
 * which is exactly what an attribute could not express.
 *
 * A TABLE BUILT HERE RATHER THAN THE SHIPPED ONE, because every one of the 398
 * shipped rows leaves the column blank on purpose -- no skill is designed to
 * differ yet, and that is the project owner's call to make. Against the shipped
 * table this test could only ever read the default back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillRowCritChanceReachesTheAbilityTest,
	"Cataclysm.WeaponSlots.ASkillRowsCriticalStrikeChanceReachesTheGrantedAbility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmSkillRowCritChanceReachesTheAbilityTest::RunTest(const FString&)
{
	using namespace CataclysmWeaponSlotsTest;

	UWorld* World = MakeWorld();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// Two Strike rows on one weapon, differing only in the Crit Chance column.
	// Shape is filled in so a real skill template is granted; a row with no shape
	// gets the placeholder, which has no chance to carry.
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FCataclysmWeaponSkillRow::StaticStruct();

	const TArray<FString> Problems = Table->CreateTableFromCSVString(TEXT(
		// PRECISE CUT STATES ALL FOUR OF ITS FIGURES AND WILD SWING STATES
		// NONE. That is the pair this test needs: one skill carrying its own
		// damage, cooldown and mana cost through to the granted instance, and
		// one leaving all three at -1 so its slot supplies them. Issue #836.
		"Name,WeaponType,DamageType,Slot,SkillName,SkillDescription,Tags,Shape,ShapeParams,CritChancePercent,DamagePercent,Cooldown,ManaCost\r\n"
		"War_Sword_Heavy,Sword,War,Heavy,Precise Cut,Cuts.,,Strike,Radius=4,20,321,7.5,42\r\n"
		"War_Sword_Special,Sword,War,Special,Wild Swing,Swings.,,Strike,Radius=4,-1,-1,-1,-1\r\n"));
	if (!TestEqual(TEXT("the table built for this test imports"), Problems.Num(), 0))
	{
		for (const FString& Problem : Problems)
		{
			AddError(Problem);
		}
		return false;
	}

	FScopedWeaponFixture Fixture(World);
	Fixture.Slots->SetWeaponSkillTable(Table);

	// THREE, NOT TWO. The two rows above fill two slots, and the weapon's basic
	// attack fills a third. Since issue #524 a basic attack comes from the Item
	// Bases sheet rather than from the weapon skill matrix, so replacing the
	// matrix does not replace it. Measured rather than assumed: this test first
	// asserted two and was wrong.
	TestEqual(TEXT("both rows filled a slot, alongside the weapon's basic attack"),
		Fixture.Slots->EquipWeaponType(TEXT("Sword")), 3);

	// Read back off the granted instances, by the skill's name, so this cannot
	// pass by looking at the table it came from.
	TMap<FString, float> Stamped;
	TMap<FString, const UCataclysmSkillTemplate*> Granted;
	for (const FGameplayAbilitySpec& Spec : Fixture.AbilitySystem->GetActivatableAbilities())
	{
		if (const UCataclysmSkillTemplate* Template =
				Cast<UCataclysmSkillTemplate>(
					const_cast<FGameplayAbilitySpec&>(Spec).GetPrimaryInstance()))
		{
			Stamped.Add(Template->SkillName, Template->CritChancePercent);
			Granted.Add(Template->SkillName, Template);
		}
	}

	if (!TestEqual(TEXT("three skill templates were granted"), Stamped.Num(), 3))
	{
		return false;
	}

	const float* Stated = Stamped.Find(TEXT("Precise Cut"));
	const float* Silent = Stamped.Find(TEXT("Wild Swing"));
	if (!TestNotNull(TEXT("the skill whose row states a chance"), Stated)
		|| !TestNotNull(TEXT("the skill whose row states none"), Silent))
	{
		return false;
	}

	TestEqual(TEXT("the skill whose row states 20% carries 20%"), *Stated, 20.0f);

	// THE TWO DISAGREE AND BOTH ARE HELD AT ONCE, which is the whole reason this
	// is not written onto the character.
	TestEqual(TEXT("and the one whose row states none still carries -1"),
		*Silent, -1.0f);

	// -- and the same chain for the three figures issue #836 added --------
	//
	// A SLOT IS A KEY, so a skill has to carry its own damage, cooldown and
	// mana cost from the table to the instance a character holds. This is the
	// step that would be dropped without noticing: the generator would still
	// write the columns, the table would still hold them, and every skill
	// would quietly go back to being worth whatever its key says.
	const UCataclysmSkillTemplate* const* StatedSkill = Granted.Find(TEXT("Precise Cut"));
	const UCataclysmSkillTemplate* const* SilentSkill = Granted.Find(TEXT("Wild Swing"));
	if (!TestNotNull(TEXT("the granted skill whose row states figures"),
					 StatedSkill ? *StatedSkill : nullptr)
		|| !TestNotNull(TEXT("the granted skill whose row states none"),
						SilentSkill ? *SilentSkill : nullptr))
	{
		return false;
	}

	TestEqual(TEXT("the skill whose row states 321% damage carries it"),
		(*StatedSkill)->DamagePercentOverride, 321.0f, 0.01f);
	TestEqual(TEXT("and asks for that rather than its slot's figure"),
		(*StatedSkill)->GetDamagePercent(), 321.0f, 0.01f);
	TestEqual(TEXT("its own cooldown of 7.5 seconds reaches it"),
		(*StatedSkill)->GetBaseCooldown(), 7.5f, 0.01f);
	TestEqual(TEXT("and its own mana cost"),
		(*StatedSkill)->ManaCostOverride, 42.0f, 0.01f);

	// THE SILENT ONE STILL TAKES ITS SLOT'S, which is the state every skill
	// in the game is in today and is what makes this landable before any
	// number is written.
	TestEqual(TEXT("the skill whose row states no damage carries -1"),
		(*SilentSkill)->DamagePercentOverride, -1.0f, 0.01f);
	TestTrue(TEXT("and asks its slot instead, which answers something"),
		(*SilentSkill)->GetDamagePercent() > 0.0f);
	TestTrue(TEXT("and takes its slot's cooldown"),
		(*SilentSkill)->GetBaseCooldown() > 0.0f);

	return true;
}

// ---------------------------------------------------------------------------
// The starting weapon is named in two places. Issue #840
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmStartingWeaponItemMatchesType,
	"Cataclysm.WeaponSlots.TheStartingWeaponItemMatchesTheStartingWeaponType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCataclysmStartingWeaponItemMatchesType::RunTest(const FString& Parameters)
{
	// TWO PLACES NAME THE STARTING WEAPON AND THEY HAVE TO AGREE.
	// ACataclysmPlayerCharacter::StartingWeaponBase names an ItemBases row and is
	// what the character actually wears since issue #840.
	// UCataclysmWeaponSlotsComponent::StartingWeaponType names a weapon type and
	// is what the fallback in OnEquipmentChanged still uses if no weapon is worn.
	//
	// IF THEY DRIFTED APART NOTHING WOULD FAIL. The character would wear one
	// weapon and, in the case where the fallback runs, be granted a different
	// weapon's six skills. Both paths would look like they worked.
	const UDataTable* BaseTable = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("the item bases table loads"), BaseTable))
	{
		return false;
	}

	const FName StartingBase =
		GetDefault<ACataclysmPlayerCharacter>()->GetStartingWeaponBase();
	if (!TestFalse(TEXT("the player character names a starting weapon item"),
			StartingBase.IsNone()))
	{
		return false;
	}

	const FCataclysmItemBaseRow* Row = BaseTable->FindRow<FCataclysmItemBaseRow>(
		StartingBase, TEXT("TheStartingWeaponItemMatchesTheStartingWeaponType"),
		/*bWarnIfMissing=*/false);
	if (!TestNotNull(TEXT("and it is a row in game/Data/ItemBases.csv"), Row))
	{
		return false;
	}

	TestFalse(TEXT("and that row is a weapon rather than a piece of armour"),
		Row->WeaponType.IsEmpty());

	TestEqual(TEXT("the starting item's weapon type is the starting weapon type"),
		Row->WeaponType,
		GetDefault<UCataclysmWeaponSlotsComponent>()->GetStartingWeaponType());

	return true;
}

#endif // WITH_AUTOMATION_TESTS
