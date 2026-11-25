// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmGameplayAbility.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmConsumptionDoubleCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Save/CataclysmSaveApply.h"
#include "Save/CataclysmSaveGather.h"
#include "Save/CataclysmSaveRecords.h"
#include "Save/CataclysmSaveStorage.h"
#include "Tests/CataclysmSaveFixtures.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

/**
 * The keys stay as they are when a weapon goes on or off, a built skill takes a key before one that does nothing, and
 * what the keys hold is saved. Issue #837, slice 1 of 2026-11-24.
 *
 * THE RULING. The owner, 2026-11-24, issue #837, chosen option verbatim: "Approve all three (Recommended)", every
 * recommendation of the plan (`plan_skill_selection.md`): Q1 the keys stay as they are when a second weapon goes on,
 * its skills joining the pool; Q2 a skill that is not built yet does not take a key a built skill made for it could;
 * Q7 a choice whose weapon is off survives a load; Q10 a row chosen for another key stays refused while a remembered
 * default simply moves.
 *
 * WHAT THESE GUARD. `UCataclysmWeaponSkills::FillWithChoices` (the remembered defaults after the choices, built rows
 * first) and `DefaultFillFrom`; `ACataclysmPlayerState::RememberDefaults`, `ForgetRememberedDefaults` and
 * `RestoreSkillChoices`; the pawn handing the defaults over and remembering after a fill from worn weapons; and the
 * character record's `RememberedDefaults` with `FCataclysmSaveApply::CharacterInto` putting the choices back before its
 * one fill.
 *
 * THE REAL TABLE, `DT_WeaponSkills`, and a possessed player that begins wearing the Greataxe, carrying Demonic, its
 * creation type. Each weapon here carries the types it is made with and nothing else.
 */
namespace CataclysmSkillDefaultsTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	const FName Demonic(TEXT("Demonic"));
	const FName PyroclasmRow(TEXT("Demonic_Greataxe_Ultimate"));

	/** The six keys a skill can be put on: every slot but the basic attack. */
	const TArray<ECataclysmAbilitySlot>& SixKeys()
	{
		static const TArray<ECataclysmAbilitySlot> Keys = {
			ECataclysmAbilitySlot::Heavy, ECataclysmAbilitySlot::Special, ECataclysmAbilitySlot::Support,
			ECataclysmAbilitySlot::Aura, ECataclysmAbilitySlot::Ultimate, ECataclysmAbilitySlot::Movement};
		return Keys;
	}

	/** The parts of one possessed player a test reads. */
	struct FDefaultsCarrier
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;

		bool IsComplete() const { return Character && State && Equipment && Slots; }
	};

	/** A possessed player wearing the Greataxe it begins with, carrying Demonic. One set-up check. */
	FDefaultsCarrier SpawnDefaultsCarrier(FAutomationTestBase& Test, UWorld* World,
										  const FVector& Where = FVector::ZeroVector)
	{
		FDefaultsCarrier Made;
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		Made.Character = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (PlayerState && Controller && Made.Character)
		{
			Controller->SetPlayerState(PlayerState);
			Controller->Possess(Made.Character);
		}
		Made.State = Made.Character ? Made.Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
		Made.Equipment = Made.Character ? Made.Character->GetEquipment() : nullptr;
		Made.Slots = Made.Character ? Made.Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
		if (!Test.TestTrue(TEXT("set-up: a possessed player with its state, equipment and weapon slots"),
						   Made.IsComplete()))
		{
			return FDefaultsCarrier();
		}
		return Made;
	}

	/** A weapon of this base carrying these types and nothing else. */
	FCataclysmItem DefaultsWeapon(const TCHAR* Base, const TArray<FName>& Types)
	{
		FCataclysmItem Made;
		Made.Base = FName(Base);
		Made.GearLevel = 0;
		Made.DamageTypes = Types;
		return Made;
	}

	/** Put a weapon on in `Slot` through the component's own equip, which announces and so fills. One check. */
	bool WearIn(FAutomationTestBase& Test, const FDefaultsCarrier& Carrier, const FCataclysmItem& Item,
				ECataclysmGearSlot Slot)
	{
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		const ECataclysmEquipResult Result = Carrier.Equipment->EquipInto(Item, Slot, Removed, AlsoRemoved);
		return Test.TestTrue(*FString::Printf(TEXT("set-up: %s goes on"), *Item.Base.ToString()),
							 Result == ECataclysmEquipResult::Equipped || Result == ECataclysmEquipResult::Swapped);
	}

	/** Take what `Slot` holds off through the component, which announces and so fills. One check. */
	bool TakeOff(FAutomationTestBase& Test, const FDefaultsCarrier& Carrier, ECataclysmGearSlot Slot)
	{
		FCataclysmItem Removed;
		return Test.TestTrue(TEXT("set-up: a weapon comes off"), Carrier.Equipment->Unequip(Slot, Removed));
	}

	/** The row on each of the six keys, None for an empty key. */
	TMap<ECataclysmAbilitySlot, FName> RowsOnKeys(const UCataclysmWeaponSlotsComponent* Slots)
	{
		TMap<ECataclysmAbilitySlot, FName> Out;
		for (const ECataclysmAbilitySlot Key : SixKeys())
		{
			Out.Add(Key, NAME_None);
		}
		for (const FCataclysmWeaponSkill& Skill : Slots->GetAvailableSkills())
		{
			if (Out.Contains(Skill.Slot))
			{
				Out[Skill.Slot] = Skill.RowName;
			}
		}
		return Out;
	}

	/** "Heavy Demonic_Axe_Heavy, Special ..." for a message. */
	FString Described(const TMap<ECataclysmAbilitySlot, FName>& Rows)
	{
		TArray<FString> Parts;
		for (const ECataclysmAbilitySlot Key : SixKeys())
		{
			const FName* Row = Rows.Find(Key);
			Parts.Add(FString::Printf(TEXT("%s %s"), *ACataclysmPlayerState::SkillSlotName(Key),
									  Row ? *Row->ToString() : TEXT("-")));
		}
		return FString::Join(Parts, TEXT(", "));
	}

	/** Every row on a key that is not an Aura belongs to `Prefix` (an Aura is shared by every weapon). */
	bool EveryWeaponKeyIs(const TMap<ECataclysmAbilitySlot, FName>& Rows, const FString& Prefix)
	{
		for (const TPair<ECataclysmAbilitySlot, FName>& Entry : Rows)
		{
			if (Entry.Key != ECataclysmAbilitySlot::Aura && !Entry.Value.ToString().StartsWith(Prefix))
			{
				return false;
			}
		}
		return true;
	}

	/** Whether any row in a list of entries is on `Key` with `Row`. */
	bool Lists(const TArray<FCataclysmSkillChoice>& Entries, ECataclysmAbilitySlot Key, FName Row)
	{
		return Entries.ContainsByPredicate([Key, Row](const FCataclysmSkillChoice& Entry)
			{ return Entry.Slot == Key && Entry.SkillRow == Row; });
	}

	/** The Greataxe off, a Demonic Axe alone in Weapon2, then a Demonic Fist put on in Weapon1. Three checks. */
	bool AxeThenFist(FAutomationTestBase& Test, const FDefaultsCarrier& Carrier)
	{
		return WearIn(Test, Carrier, DefaultsWeapon(TEXT("Weapon_Axe"), {Demonic}), ECataclysmGearSlot::Weapon2)
			&& Test.TestTrue(TEXT("set-up: the Axe alone holds the Demonic Axe's keys"),
							 EveryWeaponKeyIs(RowsOnKeys(Carrier.Slots), TEXT("Demonic_Axe_")))
			&& WearIn(Test, Carrier, DefaultsWeapon(TEXT("Weapon_Fist"), {Demonic}), ECataclysmGearSlot::Weapon1);
	}
}


// ---------------------------------------------------------------------------------------------------------------------
// SD1. A second weapon leaves every key as it was.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsSecondWeaponTest,
	"Cataclysm.SkillDefaults.ASecondWeaponLeavesEveryKeyAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Demonic Axe alone in Weapon2, then a Demonic Fist in Weapon1: the keys are still the Demonic Axe's (Q1). CONTROL:
 * the default fill of the same pool names the Fist's, so the rule before slice 1 would have moved every key.
 */
bool FCataclysmSkillDefaultsSecondWeaponTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	if (!Carrier.IsComplete() || !AxeThenFist(*this, Carrier))
	{
		return false;
	}
	const TMap<ECataclysmAbilitySlot, FName> Rows = RowsOnKeys(Carrier.Slots);
	AddInfo(FString::Printf(TEXT("SECOND WEAPON: %s"), *Described(Rows)));
	TestTrue(TEXT("**every key still holds the Demonic Axe's skill**"), EveryWeaponKeyIs(Rows, TEXT("Demonic_Axe_")));

	TMap<ECataclysmAbilitySlot, FName> Default;
	for (const FCataclysmWeaponSkill& Skill : UCataclysmWeaponSkills::DefaultFillFrom(Carrier.Slots->GetSkillPool()))
	{
		Default.Add(Skill.Slot, Skill.RowName);
	}
	TestTrue(TEXT("CONTROL: the default fill of the same pool is the Demonic Fist's"),
			 EveryWeaponKeyIs(Default, TEXT("Demonic_Fist_")));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD2. The second weapon's skills are in the pool and on no key.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsInThePoolTest,
	"Cataclysm.SkillDefaults.TheSecondWeaponsSkillsAreInThePoolAndOnNoKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** After SD1's two weapons, the Demonic Fist's five rows are in the pool and none is on a key. */
bool FCataclysmSkillDefaultsInThePoolTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	if (!Carrier.IsComplete() || !AxeThenFist(*this, Carrier))
	{
		return false;
	}
	int32 FistRows = 0;
	for (const FCataclysmWeaponSkill& Skill : Carrier.Slots->GetSkillPool())
	{
		FistRows += Skill.RowName.ToString().StartsWith(TEXT("Demonic_Fist_")) ? 1 : 0;
	}
	TestEqual(TEXT("the Demonic Fist's five rows are in the pool"), FistRows, 5);
	int32 OnKeys = 0;
	for (const TPair<ECataclysmAbilitySlot, FName>& Entry : RowsOnKeys(Carrier.Slots))
	{
		OnKeys += Entry.Value.ToString().StartsWith(TEXT("Demonic_Fist_")) ? 1 : 0;
	}
	TestEqual(TEXT("and none of them is on a key"), OnKeys, 0);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD3. Taking a weapon off changes only the keys its skills held.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsTakenOffTest,
	"Cataclysm.SkillDefaults.TakingAWeaponOffChangesOnlyTheKeysItsSkillsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Axe and the Fist, with the Fist's heavy attack chosen for the Heavy key; the Fist taken off: the Heavy key takes
 * the Axe's heavy attack, and every other key holds what it held. The choice is kept (Q5).
 */
bool FCataclysmSkillDefaultsTakenOffTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	FString Reason;
	if (!Carrier.IsComplete() || !AxeThenFist(*this, Carrier)
		|| !TestTrue(TEXT("set-up: the Fist's heavy attack is chosen for the Heavy key"),
					 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, FName(TEXT("Demonic_Fist_Heavy")), Reason)))
	{
		return false;
	}
	const TMap<ECataclysmAbilitySlot, FName> Before = RowsOnKeys(Carrier.Slots);
	if (!TakeOff(*this, Carrier, ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	const TMap<ECataclysmAbilitySlot, FName> After = RowsOnKeys(Carrier.Slots);
	AddInfo(FString::Printf(TEXT("TAKEN OFF: before %s; after %s"), *Described(Before), *Described(After)));
	TestEqual(TEXT("the Heavy key takes the Axe's heavy attack"), After[ECataclysmAbilitySlot::Heavy],
			  FName(TEXT("Demonic_Axe_Heavy")));
	for (const ECataclysmAbilitySlot Key : SixKeys())
	{
		if (Key != ECataclysmAbilitySlot::Heavy)
		{
			TestEqual(*FString::Printf(TEXT("the %s key is as it was"), *ACataclysmPlayerState::SkillSlotName(Key)),
					  After[Key], Before[Key]);
		}
	}
	TestTrue(TEXT("and the choice is kept for when the Fist returns"),
			 Lists(Carrier.State->GetSkillAssignment(), ECataclysmAbilitySlot::Heavy, FName(TEXT("Demonic_Fist_Heavy"))));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD4. A remembered default whose weapon comes off is forgotten.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsForgottenTest,
	"Cataclysm.SkillDefaults.ARememberedDefaultWhoseWeaponComesOffIsForgotten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A Demonic Axe alone; a Demonic Dagger worn over it in the same hand, so the Axe comes off and the keys become the
 * Dagger's; then the Axe put on again in the other hand: the keys stay the Dagger's, because what was remembered of the
 * Axe went when it came off.
 */
bool FCataclysmSkillDefaultsForgottenTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Axe"), {Demonic}), ECataclysmGearSlot::Weapon2)
		|| !WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Dagger"), {Demonic}), ECataclysmGearSlot::Weapon2))
	{
		return false;
	}
	TestTrue(TEXT("control: the Dagger over the Axe holds the Dagger's keys"),
			 EveryWeaponKeyIs(RowsOnKeys(Carrier.Slots), TEXT("Demonic_Dagger_")));
	if (!WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Axe"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	const TMap<ECataclysmAbilitySlot, FName> Rows = RowsOnKeys(Carrier.Slots);
	AddInfo(FString::Printf(TEXT("FORGOTTEN: %s"), *Described(Rows)));
	TestTrue(TEXT("**the Axe back in Weapon1, the keys are still the Dagger's**"),
			 EveryWeaponKeyIs(Rows, TEXT("Demonic_Dagger_")));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD5. A choice whose weapon comes off still returns with it.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsChoiceReturnsTest,
	"Cataclysm.SkillDefaults.AChoiceWhoseWeaponComesOffStillReturnsWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * SD4's control (Q5): the Axe's heavy attack CHOSEN for the Heavy key; the Dagger worn over the Axe, so the Heavy key
 * shows the Dagger's; the Axe back in the other hand: the Heavy key holds the chosen Axe skill again.
 */
bool FCataclysmSkillDefaultsChoiceReturnsTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	FString Reason;
	const FName AxeHeavy(TEXT("Demonic_Axe_Heavy"));
	if (!Carrier.IsComplete()
		|| !WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Axe"), {Demonic}), ECataclysmGearSlot::Weapon2)
		|| !TestTrue(TEXT("set-up: the Axe's heavy attack is chosen for the Heavy key"),
					 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, AxeHeavy, Reason))
		|| !WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Dagger"), {Demonic}), ECataclysmGearSlot::Weapon2))
	{
		return false;
	}
	TestEqual(TEXT("control: with the Axe off the Heavy key shows the Dagger's"),
			  RowsOnKeys(Carrier.Slots)[ECataclysmAbilitySlot::Heavy], FName(TEXT("Demonic_Dagger_Heavy")));
	if (!WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Axe"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestEqual(TEXT("**the Axe back, the Heavy key holds the chosen Axe skill**"),
			  RowsOnKeys(Carrier.Slots)[ECataclysmAbilitySlot::Heavy], AxeHeavy);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD6. A fresh bar is the creation type first, then Weapon1.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsFreshBarTest,
	"Cataclysm.SkillDefaults.AFreshBarIsTheCreationTypeFirstThenWeaponOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The regression anchor for Q11 of 2026-10-25: with nothing remembered and nothing chosen, a Fist in Weapon1 and an Axe
 * in Weapon2, both Demonic, fill the Fist's skills, the weapon in Weapon1 first. CONTROL: before the defaults are
 * forgotten the keys are the Axe's, put on first.
 */
bool FCataclysmSkillDefaultsFreshBarTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	if (!Carrier.IsComplete() || !AxeThenFist(*this, Carrier))
	{
		return false;
	}
	TestTrue(TEXT("control: remembered, the keys are the Axe's"),
			 EveryWeaponKeyIs(RowsOnKeys(Carrier.Slots), TEXT("Demonic_Axe_")));
	Carrier.State->ForgetRememberedDefaults();
	Carrier.Character->RefillAbilitySlots();
	const TMap<ECataclysmAbilitySlot, FName> Rows = RowsOnKeys(Carrier.Slots);
	AddInfo(FString::Printf(TEXT("FRESH BAR: %s"), *Described(Rows)));
	TestTrue(TEXT("**nothing remembered, the keys are the Fist's, Weapon1 first**"),
			 EveryWeaponKeyIs(Rows, TEXT("Demonic_Fist_")));
	TestEqual(TEXT("and the Aura is the creation type's"), Rows[ECataclysmAbilitySlot::Aura],
			  FName(TEXT("Demonic_All_Aura")));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD7. No weapon worn forgets the remembered defaults.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsNoWeaponTest,
	"Cataclysm.SkillDefaults.NoWeaponWornForgetsTheRememberedDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * R4: the Greataxe remembers six; everything taken off, nothing is remembered; a Demonic Axe put on alone then fills
 * the Axe's. CONTROL: before, the Greataxe's six were remembered.
 */
bool FCataclysmSkillDefaultsNoWeaponTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	TestEqual(TEXT("control: the Greataxe's six are remembered"), Carrier.State->GetRememberedDefaults().Num(), 6);
	Carrier.Equipment->UnequipEverything();
	TestEqual(TEXT("**nothing worn, nothing is remembered**"), Carrier.State->GetRememberedDefaults().Num(), 0);
	if (!WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Axe"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestTrue(TEXT("and the Axe put on alone fills its own"),
			 EveryWeaponKeyIs(RowsOnKeys(Carrier.Slots), TEXT("Demonic_Axe_")));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD8. A remembered skill chosen for another key moves, and its key takes the next.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsMovesTest,
	"Cataclysm.SkillDefaults.ASkillRememberedOnAnotherKeyMovesWhenChosenAndThatKeyTakesTheNext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * O4 and Q10: with the Axe's keys remembered beside the Fist, the Axe's heavy attack chosen for the Special key is not
 * refused: it moves there, and the Heavy key takes the next heavy attack in the pool, the Fist's. CONTROL: a skill
 * CHOSEN for another key is still refused ("Clear that slot first").
 */
bool FCataclysmSkillDefaultsMovesTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	FString Reason;
	const FName AxeHeavy(TEXT("Demonic_Axe_Heavy"));
	if (!Carrier.IsComplete() || !AxeThenFist(*this, Carrier)
		|| !TestTrue(TEXT("set-up: the Axe's heavy attack is remembered on the Heavy key"),
					 Lists(Carrier.State->GetRememberedDefaults(), ECataclysmAbilitySlot::Heavy, AxeHeavy)))
	{
		return false;
	}
	TestTrue(TEXT("**a remembered skill chosen for another key is not refused**"),
			 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Special, AxeHeavy, Reason));
	const TMap<ECataclysmAbilitySlot, FName> Rows = RowsOnKeys(Carrier.Slots);
	AddInfo(FString::Printf(TEXT("MOVED: %s (%s)"), *Described(Rows), *Reason));
	TestEqual(TEXT("it is on the Special key"), Rows[ECataclysmAbilitySlot::Special], AxeHeavy);
	TestEqual(TEXT("and the Heavy key takes the next, the Fist's"), Rows[ECataclysmAbilitySlot::Heavy],
			  FName(TEXT("Demonic_Fist_Heavy")));
	TestFalse(TEXT("and it is no longer remembered on the Heavy key"),
			  Lists(Carrier.State->GetRememberedDefaults(), ECataclysmAbilitySlot::Heavy, AxeHeavy));
	TestFalse(TEXT("CONTROL: a skill CHOSEN for another key is still refused"),
			  Carrier.State->AssignSkill(ECataclysmAbilitySlot::Ultimate, AxeHeavy, Reason));
	TestTrue(TEXT("CONTROL: saying which key to clear"), Reason.Contains(TEXT("Clear that slot first")));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD9. "Clear the slot" gives the default and remembers it.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsClearTest,
	"Cataclysm.SkillDefaults.ClearTheSlotGivesTheDefaultAndRemembersIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * O5: with the Axe's keys remembered beside the Fist, the Fist's heavy attack chosen for the Heavy key is not
 * remembered there; cleared, the key shows its default, the first heavy attack in the pool (the Fist's, Weapon1), and
 * that default is remembered.
 */
bool FCataclysmSkillDefaultsClearTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	FString Reason;
	const FName FistHeavy(TEXT("Demonic_Fist_Heavy"));
	if (!Carrier.IsComplete() || !AxeThenFist(*this, Carrier)
		|| !TestTrue(TEXT("set-up: the Fist's heavy attack is chosen for the Heavy key"),
					 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, FistHeavy, Reason)))
	{
		return false;
	}
	TestFalse(TEXT("control: a chosen key is not remembered"),
			  Carrier.State->GetRememberedDefaults().ContainsByPredicate([](const FCataclysmSkillChoice& Entry)
				  { return Entry.Slot == ECataclysmAbilitySlot::Heavy; }));
	TestTrue(TEXT("set-up: the choice is cleared"), Carrier.State->ClearSkillChoice(ECataclysmAbilitySlot::Heavy));
	TestEqual(TEXT("nothing is chosen"), Carrier.State->GetSkillAssignment().Num(), 0);
	const FName Shown = RowsOnKeys(Carrier.Slots)[ECataclysmAbilitySlot::Heavy];
	TestEqual(TEXT("the Heavy key shows its default, the first heavy attack in the pool"), Shown, FistHeavy);
	TestTrue(TEXT("**and that default is remembered**"),
			 Lists(Carrier.State->GetRememberedDefaults(), ECataclysmAbilitySlot::Heavy, Shown));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD10. A built skill takes a key before a placeholder.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsBuiltFirstTest,
	"Cataclysm.SkillDefaults.AWorkingSkillTakesAKeyBeforeAPlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q2: the owner's set of 2026-11-24, an Axe carrying Famine and War in Weapon1 and a Whip carrying War, Pestilence,
 * Death and Chaos in Weapon2, filled with nothing remembered and nothing chosen. Every key for which a BUILT skill is in
 * the pool holds a built one. CONTROL: for at least one such key the first pool entry made for it is not built, so the
 * rule before slice 1 gave it a skill that does nothing. Prints "BUILT FIRST".
 */
bool FCataclysmSkillDefaultsBuiltFirstTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	const FName War(TEXT("War"));
	if (!Carrier.IsComplete()
		|| !WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Axe"), {FName(TEXT("Famine")), War}),
				   ECataclysmGearSlot::Weapon1)
		|| !WearIn(*this, Carrier,
				   DefaultsWeapon(TEXT("Weapon_Whip"), {War, FName(TEXT("Pestilence")), FName(TEXT("Death")),
														FName(TEXT("Chaos"))}),
				   ECataclysmGearSlot::Weapon2))
	{
		return false;
	}
	Carrier.State->ForgetRememberedDefaults();
	Carrier.Character->RefillAbilitySlots();

	const TArray<FCataclysmWeaponSkill>& Pool = Carrier.Slots->GetSkillPool();
	int32 KeysAPlaceholderLedIn = 0;
	for (const ECataclysmAbilitySlot Key : SixKeys())
	{
		const FCataclysmWeaponSkill* First = Pool.FindByPredicate([Key](const FCataclysmWeaponSkill& Skill)
			{ return UCataclysmWeaponSkills::DesignedSlotOf(Skill) == Key; });
		const bool bABuiltOne = Pool.ContainsByPredicate([Key](const FCataclysmWeaponSkill& Skill)
			{ return UCataclysmWeaponSkills::DesignedSlotOf(Skill) == Key && UCataclysmWeaponSkills::IsBuilt(Skill); });
		const FCataclysmWeaponSkill* Held = Carrier.Slots->GetAvailableSkills().FindByPredicate(
			[Key](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == Key; });
		const FString KeyName = ACataclysmPlayerState::SkillSlotName(Key);
		AddInfo(FString::Printf(TEXT("BUILT FIRST: %s holds %s (%s); the pool's first for it %s (%s)"), *KeyName,
								Held ? *Held->Name : TEXT("nothing"),
								Held && UCataclysmWeaponSkills::IsBuilt(*Held) ? TEXT("built") : TEXT("not built"),
								First ? *First->Name : TEXT("nothing"),
								First && UCataclysmWeaponSkills::IsBuilt(*First) ? TEXT("built") : TEXT("not built")));
		if (bABuiltOne)
		{
			TestTrue(*FString::Printf(TEXT("**the %s key holds a built skill**"), *KeyName),
					 Held && UCataclysmWeaponSkills::IsBuilt(*Held));
			KeysAPlaceholderLedIn += First && !UCataclysmWeaponSkills::IsBuilt(*First) ? 1 : 0;
		}
	}
	TestTrue(TEXT("CONTROL: for at least one key the pool's first entry does nothing"), KeysAPlaceholderLedIn > 0);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD11. A placeholder still takes a key nothing built is made for.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsPlaceholderKeptTest,
	"Cataclysm.SkillDefaults.APlaceholderStillTakesAKeyNothingWorkingIsMadeFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * SD10's control: a War Axe alone, whose five weapon skills are not built. Each of its keys still holds one (a key with
 * something made for it stays empty only when nothing is, O4), and the not-built skills are on keys.
 */
bool FCataclysmSkillDefaultsPlaceholderKeptTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Axe"), {FName(TEXT("War"))}), ECataclysmGearSlot::Weapon2))
	{
		return false;
	}
	const TMap<ECataclysmAbilitySlot, FName> Rows = RowsOnKeys(Carrier.Slots);
	AddInfo(FString::Printf(TEXT("PLACEHOLDERS: %s"), *Described(Rows)));
	int32 NotBuiltOnKeys = 0;
	for (const FCataclysmWeaponSkill& Skill : Carrier.Slots->GetAvailableSkills())
	{
		NotBuiltOnKeys += Skill.Slot != ECataclysmAbilitySlot::BasicAttack && !UCataclysmWeaponSkills::IsBuilt(Skill) ? 1 : 0;
	}
	TestTrue(TEXT("every key the War Axe makes a skill for holds one"),
			 EveryWeaponKeyIs(Rows, TEXT("War_Axe_")));
	TestTrue(TEXT("**and its five not-built skills are on keys**"), NotBuiltOnKeys >= 5);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD12. The remembered defaults are written and put back before the fill.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsSavedTest,
	"Cataclysm.SkillDefaults.RememberedDefaultsAreWrittenAndPutBackBeforeTheFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A character wearing a Fist (Weapon1) and an Axe (Weapon2) whose keys hold the Axe's, gathered into a record and put
 * into a second player: its keys hold the Axe's. CONTROL: the default fill of that pool is the Fist's, so the keys are
 * what was remembered and not a fresh fill.
 */
bool FCataclysmSkillDefaultsSavedTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	const FDefaultsCarrier Fresh = SpawnDefaultsCarrier(*this, World, FVector(2000.0f, 0.0f, 0.0f));
	UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();
	if (!Carrier.IsComplete() || !Fresh.IsComplete() || !AxeThenFist(*this, Carrier)
		|| !TestTrue(TEXT("set-up: the record is gathered"), FCataclysmSaveGather::CharacterFrom(*Carrier.Character, *Record)))
	{
		return false;
	}
	TestTrue(TEXT("the record holds the Axe's heavy attack remembered on the Heavy key"),
			 Lists(Record->RememberedDefaults, ECataclysmAbilitySlot::Heavy, FName(TEXT("Demonic_Axe_Heavy"))));
	int32 Refused = 0;
	FString Why;
	if (!TestTrue(TEXT("set-up: the record loads into the second player"),
				  FCataclysmSaveApply::CharacterInto(*Record, *Fresh.Character, Refused, Why)))
	{
		AddError(Why);
		return false;
	}
	const TMap<ECataclysmAbilitySlot, FName> Rows = RowsOnKeys(Fresh.Slots);
	AddInfo(FString::Printf(TEXT("LOADED KEYS: %s"), *Described(Rows)));
	TestTrue(TEXT("**the loaded keys are the Axe's**"), EveryWeaponKeyIs(Rows, TEXT("Demonic_Axe_")));
	TestEqual(TEXT("and no choice was refused"), Refused, 0);
	TMap<ECataclysmAbilitySlot, FName> Default;
	for (const FCataclysmWeaponSkill& Skill : UCataclysmWeaponSkills::DefaultFillFrom(Fresh.Slots->GetSkillPool()))
	{
		Default.Add(Skill.Slot, Skill.RowName);
	}
	TestTrue(TEXT("CONTROL: the default fill of the loaded pool is the Fist's"),
			 EveryWeaponKeyIs(Default, TEXT("Demonic_Fist_")));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD13. A choice whose weapon is off survives a load.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsChoiceLoadedTest,
	"Cataclysm.SkillDefaults.AChoiceWhoseWeaponIsOffSurvivesALoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q7: Pyroclasm chosen for the Heavy key, then a Demonic Sword worn over the Greataxe, so the choice's weapon is off;
 * gathered and put into a second player, the choice is still there and nothing was refused; the Greataxe put on it, its
 * Heavy key holds Pyroclasm. CONTROL: the second player's pool has no Pyroclasm, so `AssignSkill`, which the load used
 * until slice 1, refuses it.
 */
bool FCataclysmSkillDefaultsChoiceLoadedTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	const FDefaultsCarrier Fresh = SpawnDefaultsCarrier(*this, World, FVector(2000.0f, 0.0f, 0.0f));
	UCataclysmCharacterSave* Record = NewObject<UCataclysmCharacterSave>();
	FString Reason;
	if (!Carrier.IsComplete() || !Fresh.IsComplete()
		|| !TestTrue(TEXT("set-up: Pyroclasm is chosen for the Heavy key"),
					 Carrier.State->AssignSkill(ECataclysmAbilitySlot::Heavy, PyroclasmRow, Reason))
		|| !WearIn(*this, Carrier, DefaultsWeapon(TEXT("Weapon_Sword"), {Demonic}), ECataclysmGearSlot::Weapon1)
		|| !TestTrue(TEXT("set-up: the record is gathered"), FCataclysmSaveGather::CharacterFrom(*Carrier.Character, *Record)))
	{
		return false;
	}
	int32 Refused = 0;
	FString Why;
	if (!TestTrue(TEXT("set-up: the record loads into the second player"),
				  FCataclysmSaveApply::CharacterInto(*Record, *Fresh.Character, Refused, Why)))
	{
		AddError(Why);
		return false;
	}
	TestEqual(TEXT("**no choice was refused**"), Refused, 0);
	TestTrue(TEXT("**and the choice is kept**"),
			 Lists(Fresh.State->GetSkillAssignment(), ECataclysmAbilitySlot::Heavy, PyroclasmRow));
	TestFalse(TEXT("CONTROL: the loaded pool has no Pyroclasm, so AssignSkill refuses it"),
			  ACataclysmPlayerState::RefusalForSkillChoice(ECataclysmAbilitySlot::Heavy, PyroclasmRow,
														   UCataclysmWeaponSkills::LoadGeneratedTable(),
														   Fresh.Slots->GetSkillPool(), TArray<FCataclysmSkillChoice>())
				  .IsEmpty());
	if (!WearIn(*this, Fresh, DefaultsWeapon(TEXT("Weapon_Greataxe"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestEqual(TEXT("the Greataxe back, the Heavy key holds Pyroclasm"),
			  RowsOnKeys(Fresh.Slots)[ECataclysmAbilitySlot::Heavy], PyroclasmRow);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD14. A character file from before the remembered defaults migrates with none.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsMigrationTest,
	"Cataclysm.SaveRecords.ACharacterFileFromBeforeRememberedDefaultsMigratesWithNone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `Character_v16.json` is migrated to the current version with an empty `RememberedDefaults` (`Migrate_16_to_17`), and
 * its choice kept. CONTROL: the committed `Character_v17.json` is read, not migrated, and holds the one remembered
 * default it states, War_Greatsword_Special on the Special key.
 */
bool FCataclysmSkillDefaultsMigrationTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	const auto ReadFile = [this](const TCHAR* FileName, ECataclysmSaveLoadResult& OutResult)
		-> const UCataclysmCharacterSave*
	{
		FString Text;
		FString Reason;
		if (!CataclysmSaveFixtures::Read(FileName, Text, Reason))
		{
			AddError(Reason);
			return nullptr;
		}
		FString Message;
		const UCataclysmCharacterSave* Read = Cast<UCataclysmCharacterSave>(FCataclysmSaveStorage::FromJson(
			Text, UCataclysmCharacterSave::StaticClass(), GetTransientPackage(), OutResult, Message));
		if (!Read)
		{
			AddError(FString::Printf(TEXT("%s would not load: %s"), FileName, *Message));
		}
		return Read;
	};
	ECataclysmSaveLoadResult OldResult = ECataclysmSaveLoadResult::Loaded;
	ECataclysmSaveLoadResult NewResult = ECataclysmSaveLoadResult::Migrated;
	const UCataclysmCharacterSave* Old = ReadFile(TEXT("Character_v16.json"), OldResult);
	const UCataclysmCharacterSave* Committed = ReadFile(TEXT("Character_v17.json"), NewResult);
	if (!TestNotNull(TEXT("Character_v16.json loads"), Old) || !TestNotNull(TEXT("Character_v17.json loads"), Committed))
	{
		return false;
	}
	TestEqual(TEXT("Character_v16.json is migrated"), static_cast<int32>(OldResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Migrated));
	TestEqual(TEXT("**with nothing remembered**"), Old->RememberedDefaults.Num(), 0);
	TestEqual(TEXT("and nothing remembered in its held build"), Old->ConsumptionBuild.RememberedDefaults.Num(), 0);
	TestTrue(TEXT("and its choice kept"),
			 Lists(Old->SkillAssignment, ECataclysmAbilitySlot::Ultimate, FName(TEXT("War_Greatsword_Heavy"))));
	TestEqual(TEXT("CONTROL: Character_v17.json is read, not migrated"), static_cast<int32>(NewResult),
			  static_cast<int32>(ECataclysmSaveLoadResult::Loaded));
	TestTrue(TEXT("CONTROL: and holds War_Greatsword_Special remembered on the Special key"),
			 Committed->RememberedDefaults.Num() == 1
				 && Lists(Committed->RememberedDefaults, ECataclysmAbilitySlot::Special,
						  FName(TEXT("War_Greatsword_Special"))));
	TestTrue(TEXT("CONTROL: and its held build War_Greatsword_Support on the Support key"),
			 Committed->ConsumptionBuild.RememberedDefaults.Num() == 1
				 && Lists(Committed->ConsumptionBuild.RememberedDefaults, ECataclysmAbilitySlot::Support,
						  FName(TEXT("War_Greatsword_Support"))));
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SD15. A double holds the keys the player had.
// ---------------------------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillDefaultsDoubleTest,
	"Cataclysm.SkillDefaults.ADoubleHoldsTheKeysThePlayerHad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Ruled by the coordinating session on 2026-11-24: a double holds the keys the player actually had. A character with no
 * choice at all, a Demonic Axe put on before a Demonic Fist, so its keys are the Axe's; its build captured and a double
 * made to wear it: the double's keys are the Axe's. CONTROL: the same build with its remembered defaults emptied gives
 * the double the default fill, the Fist's, Weapon1 first.
 */
bool FCataclysmSkillDefaultsDoubleTest::RunTest(const FString&)
{
	using namespace CataclysmSkillDefaultsTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FDefaultsCarrier Carrier = SpawnDefaultsCarrier(*this, World);
	if (!Carrier.IsComplete() || !AxeThenFist(*this, Carrier)
		|| !TestEqual(TEXT("set-up: nothing is chosen"), Carrier.State->GetSkillAssignment().Num(), 0))
	{
		return false;
	}
	const FCataclysmPlayerBuild Build = FCataclysmPlayerBuild::Of(*Carrier.Character);
	const auto KeysOfADoubleWearing = [this, World](const FCataclysmPlayerBuild& Worn, const FVector& Where)
	{
		TMap<ECataclysmAbilitySlot, FName> Out;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACataclysmConsumptionDoubleCharacter* Double = World->SpawnActor<ACataclysmConsumptionDoubleCharacter>(
			ACataclysmConsumptionDoubleCharacter::StaticClass(), Where, FRotator::ZeroRotator, Params);
		if (!TestNotNull(TEXT("set-up: a double was spawned"), Double)
			|| !TestTrue(TEXT("set-up: it wears the build"), Double->WearThePlayersBuild(Worn)))
		{
			return Out;
		}
		for (const ECataclysmAbilitySlot Key : SixKeys())
		{
			const FCataclysmWeaponSkill* Held = Double->SkillIn(Key);
			Out.Add(Key, Held ? Held->RowName : NAME_None);
		}
		return Out;
	};
	const TMap<ECataclysmAbilitySlot, FName> Copied = KeysOfADoubleWearing(Build, FVector(3000.0f, 0.0f, 0.0f));
	AddInfo(FString::Printf(TEXT("DOUBLE KEYS: %s"), *Described(Copied)));
	TestTrue(TEXT("**the double's keys are the Axe's, the keys the player had**"),
			 EveryWeaponKeyIs(Copied, TEXT("Demonic_Axe_")));

	FCataclysmPlayerBuild Forgetful = Build;
	Forgetful.RememberedDefaults.Reset();
	const TMap<ECataclysmAbilitySlot, FName> Fresh = KeysOfADoubleWearing(Forgetful, FVector(6000.0f, 0.0f, 0.0f));
	TestTrue(TEXT("CONTROL: with nothing remembered the double holds the default fill, the Fist's"),
			 EveryWeaponKeyIs(Fresh, TEXT("Demonic_Fist_")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
