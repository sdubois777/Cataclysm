// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmItem.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

/**
 * A character's skills come from every combination of its worn weapon types and the damage types they carry, filled
 * by default. Issue #837, layer 2, ruled 2026-10-25 (Q4 by the owner, "every combination, as the design says"; Q8 and
 * Q11 by the coordinating session).
 *
 * WHAT THESE GUARD. `UCataclysmWeaponSkills::PoolFor` (the cross product, each type's Aura once, a matched pair once)
 * and `DefaultFillFrom` (each slot's first entry); `UCataclysmWeaponSlotsComponent::InFillOrder` (the creation type
 * first when carried); the pawn handing the slots every worn weapon type in slot order and every carried type
 * (`ACataclysmPlayerCharacter::FillAbilitySlotsFromWornWeapon`); and the two readers of a character's "own" damage
 * type moved to the set question: the foreign-damage window and a triggered skill's lookup.
 *
 * WHAT THEY DO NOT GUARD. The three readers in `CataclysmDungeonGameMode.cpp` (Echo Chamber's and Wild Magic's lookups
 * and the Wild Magic panel line), which still ask `DamageTypeOf`, the first carried type: that file was outside this
 * layer's brief. Choosing a skill (layer 3) and a screen (layer 4).
 *
 * THE REAL TABLE, `DT_WeaponSkills`, and every expected list is read from `SkillsFor` on it rather than written here,
 * so a designed row changing moves both sides. Only Demonic and War have designed rows on 2026-10-25, five per weapon
 * type and one Aura each.
 */
namespace CataclysmSkillPoolTest
{
	using CataclysmTestWorld::MakeWorldThatHasBegunPlay;

	const FName Demonic(TEXT("Demonic"));
	const FName War(TEXT("War"));
	const FName VoidName(TEXT("Void"));

	/** A possessed player character in this world, or null if any part failed. The helper the creation tests use. */
	ACataclysmPlayerCharacter* SpawnPossessedPlayer(UWorld* World)
	{
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ACataclysmPlayerCharacter* Character =
			World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Character)
		{
			return nullptr;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Character);
		return Character;
	}

	/** The parts of one possessed player a test reads. */
	struct FSkillPoolCarrier
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
		UCataclysmAbilitySystemComponent* AbilitySystem = nullptr;

		bool IsComplete() const { return Character && State && Equipment && Slots && AbilitySystem; }
	};

	/** A possessed player, still wearing the two-handed Greataxe it begins with. One set-up check. */
	FSkillPoolCarrier SpawnCarrier(FAutomationTestBase& Test, UWorld* World)
	{
		FSkillPoolCarrier Made;
		Made.Character = SpawnPossessedPlayer(World);
		Made.State = Made.Character ? Made.Character->GetPlayerState<ACataclysmPlayerState>() : nullptr;
		Made.Equipment = Made.Character ? Made.Character->GetEquipment() : nullptr;
		Made.Slots = Made.Character ? Made.Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>() : nullptr;
		Made.AbilitySystem = Made.State ? Made.State->GetCataclysmAbilitySystemComponent() : nullptr;
		if (!Test.TestTrue(TEXT("set-up: a possessed player with its state, equipment, weapon slots and ability system"),
						   Made.IsComplete()))
		{
			return FSkillPoolCarrier();
		}
		return Made;
	}

	/** The same player wearing nothing, checked: the starting Greataxe fills both hands. A second set-up check. */
	FSkillPoolCarrier SpawnBareCarrier(FAutomationTestBase& Test, UWorld* World)
	{
		FSkillPoolCarrier Made = SpawnCarrier(Test, World);
		if (!Made.IsComplete())
		{
			return Made;
		}
		Made.Equipment->UnequipEverything();
		if (!Test.TestEqual(TEXT("set-up: nothing is worn"), Made.Equipment->NumEquipped(), 0))
		{
			return FSkillPoolCarrier();
		}
		return Made;
	}

	/** A weapon of this base carrying these types and nothing else. */
	FCataclysmItem Weapon(const TCHAR* Base, const TArray<FName>& Types)
	{
		FCataclysmItem Made;
		Made.Base = FName(Base);
		Made.GearLevel = 0;
		Made.DamageTypes = Types;
		return Made;
	}

	/** Put a weapon on in one slot through the component's own equip, which announces and so refills. One check. */
	bool Wear(FAutomationTestBase& Test, const FSkillPoolCarrier& Carrier, const FCataclysmItem& Item,
			  ECataclysmGearSlot Slot)
	{
		FCataclysmItem Removed;
		FCataclysmItem AlsoRemoved;
		const ECataclysmEquipResult Result = Carrier.Equipment->EquipInto(Item, Slot, Removed, AlsoRemoved);
		return Test.TestTrue(*FString::Printf(TEXT("set-up: %s goes on"), *Item.Base.ToString()),
							 Result == ECataclysmEquipResult::Equipped || Result == ECataclysmEquipResult::Swapped);
	}

	/** Every skill's name, joined, so two lists compare in order with one TestEqual of FString. */
	FString SkillNamesOf(const TArray<FCataclysmWeaponSkill>& Skills)
	{
		return FString::JoinBy(Skills, TEXT(", "), [](const FCataclysmWeaponSkill& Skill) { return Skill.Name; });
	}

	/** The skills of a list without the basic attack: what the matrix filled. */
	TArray<FCataclysmWeaponSkill> WithoutTheBasic(const TArray<FCataclysmWeaponSkill>& Skills)
	{
		return Skills.FilterByPredicate(
			[](const FCataclysmWeaponSkill& Skill) { return Skill.Slot != ECataclysmAbilitySlot::BasicAttack; });
	}

	/** How many of a list's skills sit in this slot. */
	int32 HowManyIn(const TArray<FCataclysmWeaponSkill>& Skills, ECataclysmAbilitySlot Slot)
	{
		return Skills.FilterByPredicate([Slot](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == Slot; }).Num();
	}

	/** Whether every skill of `Wanted` is in `Pool`, by name. */
	bool HoldsEvery(const TArray<FCataclysmWeaponSkill>& Pool, const TArray<FCataclysmWeaponSkill>& Wanted)
	{
		for (const FCataclysmWeaponSkill& Skill : Wanted)
		{
			if (!Pool.ContainsByPredicate([&Skill](const FCataclysmWeaponSkill& Held) { return Held.Name == Skill.Name; }))
			{
				return false;
			}
		}
		return Wanted.Num() > 0;
	}

	/** A list of names as one string, so two lists compare with one TestEqual of FString. */
	FString Joined(const TArray<FString>& Names)
	{
		return FString::Join(Names, TEXT(", "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillPoolTodaysSixTest,
	"Cataclysm.SkillPool.OneWeaponOfOneTypeFillsTodaysSixInTodaysOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE REGRESSION ANCHOR. One weapon of one type pools exactly what `SkillsFor` gave before layer 2, in its order, and
 * the slots fill the basic attack and those six. On the pure functions, on a bare component a test pins a type on,
 * and on a possessed player wearing its starting Greataxe, which carries the creation type, Demonic.
 */
bool FCataclysmSkillPoolTodaysSixTest::RunTest(const FString&)
{
	using namespace CataclysmSkillPoolTest;

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	const UDataTable* Bases = UCataclysmItemModifiers::LoadBaseTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill matrix loads"), Table)
		|| !TestNotNull(TEXT("set-up: the item bases load"), Bases))
	{
		return false;
	}

	// THE CONTROL: the anchor is not empty, so the comparisons below cannot pass on two empty lists.
	TestEqual(TEXT("control: a Demonic Greataxe offers six designed skills"),
			  UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Greataxe"), TEXT("Demonic")).Num(), 6);

	const TCHAR* Pairs[][2] = {
		{TEXT("Greataxe"), TEXT("Demonic")}, {TEXT("Sword"), TEXT("War")}, {TEXT("Shield"), TEXT("War")}};
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	for (const auto& Pair : Pairs)
	{
		const FString WeaponType = Pair[0];
		const FString DamageType = Pair[1];
		const TArray<FCataclysmWeaponSkill> Today = UCataclysmWeaponSkills::SkillsFor(Table, WeaponType, DamageType);
		const TArray<FCataclysmWeaponSkill> Pool =
			UCataclysmWeaponSkills::PoolFor(Table, {WeaponType}, {DamageType});
		TestEqual(*FString::Printf(TEXT("a %s %s pools what SkillsFor gave, in its order"), *DamageType, *WeaponType),
				  SkillNamesOf(Pool), SkillNamesOf(Today));
		TestEqual(*FString::Printf(TEXT("and the %s %s's default fill is those six"), *DamageType, *WeaponType),
				  SkillNamesOf(UCataclysmWeaponSkills::DefaultFillFrom(Pool)), SkillNamesOf(Today));

		// ON A BARE COMPONENT, the type pinned as earlier tests pin it: the basic attack first, then the six.
		AActor* Actor = World->SpawnActor<AActor>();
		UCataclysmWeaponSlotsComponent* Slots = Actor ? NewObject<UCataclysmWeaponSlotsComponent>(Actor) : nullptr;
		if (!TestNotNull(TEXT("set-up: a bare weapon slots component"), Slots))
		{
			return false;
		}
		Slots->RegisterComponent();
		Slots->SetDamageType(DamageType);
		Slots->EquipWeaponType(WeaponType);
		const FCataclysmWeaponSkill Basic = UCataclysmWeaponSkills::BasicAttackFor(Bases, WeaponType);
		TArray<FCataclysmWeaponSkill> Expected;
		if (Basic.Slot != ECataclysmAbilitySlot::None)
		{
			Expected.Add(Basic);
		}
		Expected.Append(Today);
		TestEqual(*FString::Printf(TEXT("a component holding a %s %s fills the basic attack and the six"), *DamageType,
								   *WeaponType),
				  SkillNamesOf(Slots->GetAvailableSkills()), SkillNamesOf(Expected));
		Actor->Destroy();
	}

	// ON A POSSESSED PLAYER, wearing the Greataxe it begins with: the play case.
	const FSkillPoolCarrier Carrier = SpawnCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	TestEqual(TEXT("the starting Greataxe carries the creation type alone"),
			  Joined(Carrier.Slots->GetDamageTypes()), FString(TEXT("Demonic")));
	const TArray<FCataclysmWeaponSkill> Held = WithoutTheBasic(Carrier.Slots->GetAvailableSkills());
	TestEqual(TEXT("the player's six are the Demonic Greataxe's six, in their order"), SkillNamesOf(Held),
			  SkillNamesOf(UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Greataxe"), TEXT("Demonic"))));
	TestEqual(TEXT("and its pool is those six and no more"), Carrier.Slots->GetSkillPool().Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillPoolTwoTypesTest,
	"Cataclysm.SkillPool.OneWeaponOfTwoTypesPoolsBoth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * One weapon carrying Demonic and War pools both types' rows for that weapon and both Auras: twelve. The control is the
 * one type alone, six, and a third type with no designed row, Void, which adds nothing. On a possessed player too.
 */
bool FCataclysmSkillPoolTwoTypesTest::RunTest(const FString&)
{
	using namespace CataclysmSkillPoolTest;

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill matrix loads"), Table))
	{
		return false;
	}

	// THE CONTROL: ONE TYPE.
	TestEqual(TEXT("control: a Greataxe carrying Demonic pools six"),
			  UCataclysmWeaponSkills::PoolFor(Table, {TEXT("Greataxe")}, {TEXT("Demonic")}).Num(), 6);

	const TArray<FCataclysmWeaponSkill> Both =
		UCataclysmWeaponSkills::PoolFor(Table, {TEXT("Greataxe")}, {TEXT("Demonic"), TEXT("War")});
	TestEqual(TEXT("carrying Demonic and War it pools twelve"), Both.Num(), 12);
	TestEqual(TEXT("two of them Auras, one for each type"), HowManyIn(Both, ECataclysmAbilitySlot::Aura), 2);
	TestTrue(TEXT("every Demonic Greataxe skill is in it"),
			 HoldsEvery(Both, UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Greataxe"), TEXT("Demonic"))));
	TestTrue(TEXT("and every War Greataxe skill"),
			 HoldsEvery(Both, UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Greataxe"), TEXT("War"))));

	// THE CONTROL ON THE OTHER SIDE: A TYPE WITH NO DESIGNED ROW ADDS NOTHING, not even an Aura.
	TestEqual(TEXT("control: Void has no designed Greataxe row"),
			  UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Greataxe"), TEXT("Void")).Num(), 0);
	TestEqual(TEXT("so adding it leaves twelve"),
			  UCataclysmWeaponSkills::PoolFor(Table, {TEXT("Greataxe")}, {TEXT("Demonic"), TEXT("War"), TEXT("Void")})
				  .Num(),
			  12);

	// ON A POSSESSED PLAYER: the worn weapon's two types reach the pool.
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillPoolCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {Demonic, War}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestEqual(TEXT("the player's slots carry Demonic and War"), Joined(Carrier.Slots->GetDamageTypes()),
			  FString(TEXT("Demonic, War")));
	TestEqual(TEXT("and its pool is twelve"), Carrier.Slots->GetSkillPool().Num(), 12);
	TestEqual(TEXT("from which seven slots are filled, the basic attack among them"),
			  Carrier.Slots->GetAvailableSkills().Num(), 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillPoolMatchedPairTest,
	"Cataclysm.SkillPool.AMatchedPairPoolsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Two Swords carrying War pool what one does: six, the same six. The design: "A MATCHED PAIR OF WEAPONS CHANGES NOTHING
 * HERE." The control is a Sword beside an Axe, which pools eleven: two weapons' five and one Aura.
 */
bool FCataclysmSkillPoolMatchedPairTest::RunTest(const FString&)
{
	using namespace CataclysmSkillPoolTest;

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill matrix loads"), Table))
	{
		return false;
	}

	// THE CONTROL: TWO DIFFERENT WEAPONS DO ADD.
	TestEqual(TEXT("control: a War Sword beside a War Axe pools eleven"),
			  UCataclysmWeaponSkills::PoolFor(Table, {TEXT("Sword"), TEXT("Axe")}, {TEXT("War")}).Num(), 11);

	const TArray<FCataclysmWeaponSkill> Pair =
		UCataclysmWeaponSkills::PoolFor(Table, {TEXT("Sword"), TEXT("Sword")}, {TEXT("War")});
	TestEqual(TEXT("two War Swords pool the one Sword's six, in its order"), SkillNamesOf(Pair),
			  SkillNamesOf(UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Sword"), TEXT("War"))));

	// ON A POSSESSED PLAYER, one Sword in each hand.
	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillPoolCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Sword"), {War}), ECataclysmGearSlot::Weapon1)
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Sword"), {War}), ECataclysmGearSlot::Weapon2)
		|| !TestEqual(TEXT("set-up: two weapons are worn"), Carrier.Equipment->WornWeapons().Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("the player's slots were handed both Swords"), Joined(Carrier.Slots->GetWornWeaponTypes()),
			  FString(TEXT("Sword, Sword")));
	TestEqual(TEXT("and pool six"), Carrier.Slots->GetSkillPool().Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillPoolEveryCombinationTest,
	"Cataclysm.SkillPool.TwoWeaponsPoolEveryCombinationOfWeaponAndType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q4, the owner's "every combination, as the design says": a Sword carrying Demonic beside an Axe carrying War pools
 * the Demonic Sword's and the War Axe's rows AND the War Sword's and the Demonic Axe's, and both Auras: twenty-two. The
 * control is the Sword alone, six.
 */
bool FCataclysmSkillPoolEveryCombinationTest::RunTest(const FString&)
{
	using namespace CataclysmSkillPoolTest;

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill matrix loads"), Table))
	{
		return false;
	}
	TestEqual(TEXT("on the pure function, two weapons and two types pool twenty-two"),
			  UCataclysmWeaponSkills::PoolFor(Table, {TEXT("Sword"), TEXT("Axe")}, {TEXT("Demonic"), TEXT("War")}).Num(),
			  22);

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillPoolCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete()
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Sword"), {Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}

	// THE CONTROL: ONE WEAPON.
	TestEqual(TEXT("control: the Sword carrying Demonic alone pools six"), Carrier.Slots->GetSkillPool().Num(), 6);

	if (!Wear(*this, Carrier, Weapon(TEXT("Weapon_Axe"), {War}), ECataclysmGearSlot::Weapon2)
		|| !TestEqual(TEXT("set-up: two weapons are worn"), Carrier.Equipment->WornWeapons().Num(), 2))
	{
		return false;
	}
	const TArray<FCataclysmWeaponSkill>& Pool = Carrier.Slots->GetSkillPool();
	TestEqual(TEXT("with the Axe carrying War beside it the pool is twenty-two"), Pool.Num(), 22);
	TestTrue(TEXT("it holds the War Sword's rows, a combination neither weapon carries alone"),
			 HoldsEvery(Pool, UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Sword"), TEXT("War"))));
	TestTrue(TEXT("and the Demonic Axe's"),
			 HoldsEvery(Pool, UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Axe"), TEXT("Demonic"))));
	TestTrue(TEXT("as well as the Demonic Sword's"),
			 HoldsEvery(Pool, UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Sword"), TEXT("Demonic"))));
	TestTrue(TEXT("and the War Axe's"),
			 HoldsEvery(Pool, UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Axe"), TEXT("War"))));
	TestEqual(TEXT("with one Aura for each type"), HowManyIn(Pool, ECataclysmAbilitySlot::Aura), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillPoolFillOrderTest,
	"Cataclysm.SkillPool.TheDefaultFillTakesTheCreationTypeFirstThenTheWornWeaponsInSlotOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q11: the creation type first when it is carried, then the other carried types; within a type the worn weapons in
 * `WornWeapons` order, the first OCCUPIED slot first. A Greataxe carrying War then Demonic, on a character created
 * Demonic, fills the Demonic six; created War, the War six (the control). A War Sword in Weapon1 and a War Axe in
 * Weapon2 fill the Sword's; swapped, the Axe's; and a War Axe alone in Weapon2 fills the Axe's.
 *
 * THE LONE WEAPON IS PUT IN WEAPON2 BY `EquipInto`, with Weapon1 empty and no two-handed weapon taken off. Before the
 * branch `fix/lone-one-hander-goes-to-the-right-hand` lands, a lone one-hander chosen at creation is in Weapon2 in play
 * (measured in window t88x: "Weapon1 holds nothing and Weapon2 holds Weapon_Sword"); after it, in Weapon1. That rule is
 * about a one-hander that takes a two-hander off, which this set-up does not do, so this stays a Weapon2 case either
 * way; if it does not, its set-up check says so, and moving the weapon is a set-up change, not an assertion change.
 */
bool FCataclysmSkillPoolFillOrderTest::RunTest(const FString&)
{
	using namespace CataclysmSkillPoolTest;

	const UDataTable* Table = UCataclysmWeaponSkills::LoadGeneratedTable();
	if (!TestNotNull(TEXT("set-up: the weapon skill matrix loads"), Table))
	{
		return false;
	}

	// ON THE STATIC: THE CREATION TYPE MOVES TO THE FRONT ONLY WHEN IT IS CARRIED.
	TestEqual(TEXT("control: War then Void, created Demonic: as carried, and Demonic not added"),
			  Joined(UCataclysmWeaponSlotsComponent::InFillOrder({War, VoidName}, Demonic)), FString(TEXT("War, Void")));
	TestEqual(TEXT("War then Demonic, created Demonic: Demonic first"),
			  Joined(UCataclysmWeaponSlotsComponent::InFillOrder({War, Demonic}, Demonic)), FString(TEXT("Demonic, War")));

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillPoolCarrier Carrier = SpawnBareCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	const auto HeldSix = [&Carrier]() { return SkillNamesOf(WithoutTheBasic(Carrier.Slots->GetAvailableSkills())); };
	const FString DemonicAxe = SkillNamesOf(UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Greataxe"), TEXT("Demonic")));
	const FString WarAxe = SkillNamesOf(UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Greataxe"), TEXT("War")));

	// THE CONTROL: CREATED WAR, and the weapon lists War first, so either order gives War first.
	Carrier.State->SetCreationChoice(FName(TEXT("Greataxe")), War);
	if (!Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {War, Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestEqual(TEXT("control: created War, the slots carry War first"), Joined(Carrier.Slots->GetDamageTypes()),
			  FString(TEXT("War, Demonic")));
	TestEqual(TEXT("control: and fill the War Greataxe's six"), HeldSix(), WarAxe);

	// CREATED DEMONIC, the same weapon put on again: it lists War first and the creation type still goes first.
	Carrier.State->SetCreationChoice(FName(TEXT("Greataxe")), Demonic);
	Carrier.Equipment->UnequipEverything();
	if (!TestEqual(TEXT("set-up: nothing is worn"), Carrier.Equipment->NumEquipped(), 0)
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Greataxe"), {War, Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestEqual(TEXT("created Demonic, the slots carry Demonic first"), Joined(Carrier.Slots->GetDamageTypes()),
			  FString(TEXT("Demonic, War")));
	TestEqual(TEXT("and fill the Demonic Greataxe's six"), HeldSix(), DemonicAxe);

	// WITHIN A TYPE, THE WORN WEAPONS IN SLOT ORDER.
	const FString WarSword = SkillNamesOf(UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Sword"), TEXT("War")));
	const FString WarAxeOneHanded = SkillNamesOf(UCataclysmWeaponSkills::SkillsFor(Table, TEXT("Axe"), TEXT("War")));
	Carrier.Equipment->UnequipEverything();
	if (!TestEqual(TEXT("set-up: nothing is worn"), Carrier.Equipment->NumEquipped(), 0)
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Sword"), {War}), ECataclysmGearSlot::Weapon1)
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Axe"), {War}), ECataclysmGearSlot::Weapon2))
	{
		return false;
	}
	TestEqual(TEXT("a War Sword in Weapon1 and a War Axe in Weapon2 fill the Sword's six"), HeldSix(), WarSword);

	Carrier.Equipment->UnequipEverything();
	if (!TestEqual(TEXT("set-up: nothing is worn"), Carrier.Equipment->NumEquipped(), 0)
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Axe"), {War}), ECataclysmGearSlot::Weapon1)
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Sword"), {War}), ECataclysmGearSlot::Weapon2))
	{
		return false;
	}
	TestEqual(TEXT("swapped, the Axe's six"), HeldSix(), WarAxeOneHanded);

	// A LONE WEAPON IN WEAPON2 IS THE FIRST WORN WEAPON.
	Carrier.Equipment->UnequipEverything();
	if (!TestEqual(TEXT("set-up: nothing is worn"), Carrier.Equipment->NumEquipped(), 0)
		|| !Wear(*this, Carrier, Weapon(TEXT("Weapon_Axe"), {War}), ECataclysmGearSlot::Weapon2))
	{
		return false;
	}
	const FCataclysmItem* InWeapon1 = Carrier.Equipment->EquippedAt(ECataclysmGearSlot::Weapon1);
	const FCataclysmItem* InWeapon2 = Carrier.Equipment->EquippedAt(ECataclysmGearSlot::Weapon2);
	AddInfo(FString::Printf(TEXT("set-up: for the lone weapon Weapon1 holds %s and Weapon2 holds %s"),
							InWeapon1 ? *InWeapon1->Base.ToString() : TEXT("nothing"),
							InWeapon2 ? *InWeapon2->Base.ToString() : TEXT("nothing")));
	if (!TestTrue(TEXT("set-up: the Axe is alone, in Weapon2"),
				  !InWeapon1 && InWeapon2 && InWeapon2->Base == FName(TEXT("Weapon_Axe"))))
	{
		return false;
	}
	TestEqual(TEXT("a War Axe alone in Weapon2 fills the Axe's six"), HeldSix(), WarAxeOneHanded);
	TestEqual(TEXT("and its basic attack's weapon is the Axe"), Carrier.Slots->GetEquippedWeaponType(),
			  FString(TEXT("Axe")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillPoolForeignWindowTest,
	"Cataclysm.SkillPool.AHitOfAnyCarriedTypeIsTheCharactersOwnAndOpensNoForeignWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q8 on the first reader, `UCataclysmVitalAttributeSet`'s foreign-damage window (Cataclysmic Resonance): a character
 * carrying Demonic and War is not hit by a foreign type when struck by War, and is when struck by Void. The control is
 * the same character carrying Demonic alone, whom the War hit opens the window for.
 *
 * A BARE DEFENDER WITH ITS OWN ABILITY SYSTEM, as `Cataclysm.ConditionalDamage.DamageOfAnotherCataclysmTypeOpensAWindow`
 * builds one, and the types handed to its weapon slots as the pawn hands them. The world's clock is moved by hand so a
 * window that did not reopen reads its age.
 */
bool FCataclysmSkillPoolForeignWindowTest::RunTest(const FString&)
{
	using namespace CataclysmSkillPoolTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE DEFENDER, AT THE ORIGIN: nothing of its own stops a hit.
	AActor* Defender = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("set-up: a defender"), Defender))
	{
		return false;
	}
	UCataclysmAbilitySystemComponent* Abilities = NewObject<UCataclysmAbilitySystemComponent>(Defender);
	Abilities->RegisterComponent();
	UCataclysmCombatAttributeSet* Combat = NewObject<UCataclysmCombatAttributeSet>(Defender);
	UCataclysmVitalAttributeSet* Vitals = NewObject<UCataclysmVitalAttributeSet>(Defender);
	Abilities->AddAttributeSetSubobject(Combat);
	Abilities->AddAttributeSetSubobject(Vitals);
	Abilities->InitAbilityActorInfo(Defender, Defender);
	Combat->SetArmor(0.0f);
	Combat->SetEvasion(0.0f);
	Combat->SetBlockChance(0.0f);
	Combat->SetDamageReduction(0.0f);
	Vitals->SetMaxHealth(1'000'000.0f);
	Vitals->SetHealth(1'000'000.0f);
	UCataclysmWeaponSlotsComponent* Slots = NewObject<UCataclysmWeaponSlotsComponent>(Defender);
	Slots->RegisterComponent();

	// TWO ATTACKERS, FIVE METRES EITHER SIDE.
	const auto Attacker = [World](const FVector& Where, FName Type)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->DamageType = Type;
			if (UAbilitySystemComponent* System = Made->GetAbilitySystemComponent())
			{
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 1'000.0f);
			}
		}
		return Made;
	};
	ACataclysmEnemyCharacter* WarAttacker = Attacker(FVector(500.0f, 0.0f, 0.0f), War);
	ACataclysmEnemyCharacter* VoidAttacker = Attacker(FVector(-500.0f, 0.0f, 0.0f), VoidName);
	if (!TestNotNull(TEXT("set-up: a War attacker"), WarAttacker)
		|| !TestNotNull(TEXT("set-up: a Void attacker"), VoidAttacker))
	{
		return false;
	}
	const auto Strike = [this, Defender, Vitals](ACataclysmEnemyCharacter* By)
	{
		const float Before = Vitals->GetHealth();
		UCataclysmSkillEffects::ApplyHit(By, Defender, /*DamagePercent=*/100.0f);
		return TestTrue(TEXT("set-up: the hit reached the defender"), Vitals->GetHealth() < Before);
	};

	// THE CONTROL: CARRYING DEMONIC ALONE, a War hit is foreign.
	Slots->UseDamageTypes({TEXT("Demonic")});
	TestFalse(TEXT("control: carrying Demonic alone, War is not its own"),
			  UCataclysmWeaponSlotsComponent::CarriesDamageType(Defender, War));
	TestEqual(TEXT("control: nothing has opened the window yet"), Abilities->SecondsSinceForeignDamageTaken(), -1.0f,
			  0.001f);
	if (!Strike(WarAttacker))
	{
		return false;
	}
	TestEqual(TEXT("control: a War hit on a Demonic carrier opens the window, now"),
			  Abilities->SecondsSinceForeignDamageTaken(), 0.0f, 0.001f);
	World->TimeSeconds += 6.0f;
	TestEqual(TEXT("control: and six seconds later it is six seconds old"), Abilities->SecondsSinceForeignDamageTaken(),
			  6.0f, 0.01f);

	// CARRYING DEMONIC AND WAR: a War hit is its own, and the window is not opened again.
	Slots->UseDamageTypes({TEXT("Demonic"), TEXT("War")});
	TestTrue(TEXT("carrying Demonic and War, War is its own"),
			 UCataclysmWeaponSlotsComponent::CarriesDamageType(Defender, War));
	if (!Strike(WarAttacker))
	{
		return false;
	}
	TestEqual(TEXT("a War hit on a Demonic and War carrier opens nothing: the window is still six seconds old"),
			  Abilities->SecondsSinceForeignDamageTaken(), 6.0f, 0.01f);

	// AND A TYPE IT DOES NOT CARRY STILL OPENS IT.
	if (!Strike(VoidAttacker))
	{
		return false;
	}
	TestEqual(TEXT("a Void hit on the same carrier opens it, now"), Abilities->SecondsSinceForeignDamageTaken(), 0.0f,
			  0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillPoolRepeatLookupTest,
	"Cataclysm.SkillPool.ARepeatFindsASkillOfTheSecondCarriedType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Q8 on the second reader, `UCataclysmTriggeredSkill`'s lookup of a used skill's row (`FindTheSkillUsed`, which a
 * repeat row and a kept-stack cast both ask): a skill of any carried type is found. Carom is a Demonic skill. A
 * possessed player carrying War alone cannot repeat it (the control), and carrying War then Demonic can, though War is
 * the first carried type and `DamageTypeOf` names War.
 *
 * THE REPEAT ROW IS MADE BY HAND, as `Cataclysm.Enchantments.ARepeatRowRepeatsTheSkillJustUsedFreeAtTheSameAim` makes
 * it, with the roll pinned. Nothing is worn or taken off after the row is handed over, so no refresh replaces it.
 */
bool FCataclysmSkillPoolRepeatLookupTest::RunTest(const FString&)
{
	using namespace CataclysmSkillPoolTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FSkillPoolCarrier Carrier = SpawnCarrier(*this, World);
	if (!Carrier.IsComplete())
	{
		return false;
	}
	const TArray<FCataclysmWeaponSkill> DemonicSkills =
		UCataclysmWeaponSkills::SkillsOfDamageType(UCataclysmWeaponSkills::LoadGeneratedTable(), TEXT("Demonic"));
	const FCataclysmWeaponSkill* Carom = DemonicSkills.FindByPredicate(
		[](const FCataclysmWeaponSkill& Skill) { return Skill.Name == TEXT("Carom"); });
	IConsoleVariable* Roll = IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.RepeatSkillRoll"));
	if (!TestNotNull(TEXT("set-up: Carom is a Demonic skill"), Carom)
		|| !TestNotNull(TEXT("set-up: the repeat roll can be pinned"), Roll))
	{
		return false;
	}
	Roll->Set(TEXT("0"), ECVF_SetByConsole);
	ON_SCOPE_EXIT { Roll->Set(TEXT("-1"), ECVF_SetByConsole); };

	FCataclysmPoolAction Repeat;
	Repeat.Event = FName(TEXT("skill_use"));
	Repeat.Percent = 15.0f;
	Repeat.bRepeatSkill = true;
	Repeat.RepeatSharePercent = 100.0f;
	Repeat.TriggerKey = FName(TEXT("Test:repeat"));
	Carrier.AbilitySystem->SetPoolActions({Repeat});

	const FVector Aim = Carrier.Character->GetActorLocation() + FVector(900.0f, 300.0f, 0.0f);
	const auto UseCarom = [&Carrier, Carom, &Aim]()
	{
		UCataclysmCombatEvents::NoteSkillUsed(Carrier.Character, Carom->Name, Carom->Tags, ECataclysmAbilitySlot::Heavy,
											  &Aim);
	};

	// THE CONTROL: CARRYING WAR ALONE, Carom is not one of its skills, and nothing is repeated.
	Carrier.Slots->UseDamageTypes({TEXT("War")});
	UseCarom();
	TestEqual(TEXT("control: the use records a repeat of Carom"), Carrier.AbilitySystem->PendingRepeatSkill(),
			  FName(*Carom->Name));
	TestFalse(TEXT("control: carrying War alone, the repeat is not made"),
			  UCataclysmTriggeredSkill::MakePendingRepeat(Carrier.Character));

	// CARRYING WAR AND THEN DEMONIC: War first, as `DamageTypeOf` reads it, and Carom is found under Demonic.
	Carrier.Slots->UseDamageTypes({TEXT("War"), TEXT("Demonic")});
	TestEqual(TEXT("War is the first carried type"), UCataclysmWeaponSlotsComponent::DamageTypeOf(Carrier.Character),
			  FString(TEXT("War")));
	UseCarom();
	TestEqual(TEXT("the use records a repeat of Carom again"), Carrier.AbilitySystem->PendingRepeatSkill(),
			  FName(*Carom->Name));
	TestTrue(TEXT("carrying War and Demonic, the repeat is made"),
			 UCataclysmTriggeredSkill::MakePendingRepeat(Carrier.Character));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCataclysmSkillPoolPinnedTypeTest,
	"Cataclysm.SkillPool.ATestsPinnedDamageTypeHoldsThroughAChangeOfGear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * `SetDamageType` is the seam earlier tests set up with, and it holds: a possessed player pinned to War keeps War
 * when a weapon carrying Demonic goes on, as it did when the type was one field nothing else wrote. The control is the
 * same weapon on a player nothing pinned, whose slots take the weapon's Demonic.
 */
bool FCataclysmSkillPoolPinnedTypeTest::RunTest(const FString&)
{
	using namespace CataclysmSkillPoolTest;

	UWorld* World = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// THE CONTROL: NOTHING PINNED.
	const FSkillPoolCarrier Plain = SpawnBareCarrier(*this, World);
	if (!Plain.IsComplete()
		|| !Wear(*this, Plain, Weapon(TEXT("Weapon_Sword"), {VoidName, Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestEqual(TEXT("control: unpinned, the slots take the weapon's types, the creation type first"),
			  Joined(Plain.Slots->GetDamageTypes()), FString(TEXT("Demonic, Void")));

	// PINNED, IN A SECOND WORLD SO THE TWO PLAYERS ARE NOT NEIGHBOURS.
	UWorld* Second = MakeWorldThatHasBegunPlay();
	ON_SCOPE_EXIT { Second->DestroyWorld(false); };
	const FSkillPoolCarrier Pinned = SpawnBareCarrier(*this, Second);
	if (!Pinned.IsComplete())
	{
		return false;
	}
	Pinned.Slots->SetDamageType(TEXT("War"));
	if (!Wear(*this, Pinned, Weapon(TEXT("Weapon_Sword"), {VoidName, Demonic}), ECataclysmGearSlot::Weapon1))
	{
		return false;
	}
	TestEqual(TEXT("pinned to War, the slots keep War through the change of gear"),
			  Joined(Pinned.Slots->GetDamageTypes()), FString(TEXT("War")));
	TestEqual(TEXT("and fill the War Sword's six"), SkillNamesOf(WithoutTheBasic(Pinned.Slots->GetAvailableSkills())),
			  SkillNamesOf(UCataclysmWeaponSkills::SkillsFor(UCataclysmWeaponSkills::LoadGeneratedTable(), TEXT("Sword"),
														 TEXT("War"))));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
