// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmUndesignedSkill.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
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
	TestEqual(TEXT("a Dagger fills six slots"), DaggerFilled, 6);
	TestEqual(TEXT("and grants exactly six abilities"), Fixture.GrantedCount(), 6);

	const TSet<FGameplayTag> DaggerTags = Fixture.GrantedSlotTags();
	TestEqual(TEXT("six distinct slot tags, so no key fires two abilities"),
		DaggerTags.Num(), 6);

	TArray<FString> DaggerSkills;
	for (const FCataclysmWeaponSkill& Skill : Fixture.Slots->GetAvailableSkills())
	{
		DaggerSkills.Add(Skill.Name);
	}

	// A different weapon type. No code below names a skill; the change comes
	// entirely from the matrix.
	const int32 WarhammerFilled = Fixture.Slots->EquipWeaponType(TEXT("Warhammer"));
	TestEqual(TEXT("a Warhammer also fills six slots"), WarhammerFilled, 6);

	TestEqual(TEXT("and the Dagger's abilities are gone, not still granted"),
		Fixture.GrantedCount(), 6);

	TestEqual(TEXT("still six distinct slot tags"),
		Fixture.GrantedSlotTags().Num(), 6);

	// The same six slots are filled, by different skills. That is what makes a
	// weapon drop change how the character plays rather than only its numbers.
	TestTrue(TEXT("the same six slots are filled"),
		Fixture.GrantedSlotTags().Includes(DaggerTags));

	int32 Shared = 0;
	for (const FCataclysmWeaponSkill& Skill : Fixture.Slots->GetAvailableSkills())
	{
		if (DaggerSkills.Contains(Skill.Name))
		{
			++Shared;
		}
	}

	// One shared: the Aura is weapon-independent, so both weapons offer it. If
	// this were six, equipping a different weapon would have changed nothing.
	TestEqual(TEXT("only the weapon-independent Aura skill is shared"), Shared, 1);

	Fixture.Slots->UnequipWeapon();
	TestEqual(TEXT("unequipping takes every ability back"), Fixture.GrantedCount(), 0);

	return true;
}

/**
 * A weapon with no skills grants nothing and leaves nothing behind.
 *
 * The dangerous shape is equipping a covered weapon and then an uncovered one:
 * if unequipping did not happen first, the character would keep the old
 * weapon's abilities while holding a weapon that offers none.
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
	TestEqual(TEXT("a Sword fills six slots"), Fixture.GrantedCount(), 6);

	// War covers no magic weapons, so this grants nothing.
	const int32 Filled = Fixture.Slots->EquipWeaponType(TEXT("Wand"));
	TestEqual(TEXT("a War Wand fills none"), Filled, 0);
	TestEqual(TEXT("and the Sword's abilities went with it"),
		Fixture.GrantedCount(), 0);
	TestEqual(TEXT("and it reports no available skills"),
		Fixture.Slots->GetAvailableSkills().Num(), 0);

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
			TEXT("a Demonic %s fills all six slots"), Weapon), Filled, 6);
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
			TEXT("a Demonic %s fills all six slots"), Weapon), Filled, 6);
	}

	// A weapon type Demonic does not cover still grants nothing, which is what
	// keeps the check above from passing for any string at all.
	const int32 NotCovered = Slots->EquipWeaponType(TEXT("Crossbow"));
	TestEqual(TEXT("Demonic does not cover the Crossbow, so it grants nothing"),
		NotCovered, 0);

	Actor->Destroy();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
