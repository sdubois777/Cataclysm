// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "AbilitySystemComponent.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"

/**
 * The Saboteur's device conditions, 2026-11-25, on the coordinating session's rulings of that day: nodes whose figures
 * hold while, or for some seconds after, something happens to the player's traps and gadgets.
 *
 * TWO KINDS OF PLAYER. Razor Instincts, Sentry Network and Overcautious are read through THE GENERATED TABLE, as the game
 * loads it: a War player allocated the node and refreshed, so the row, the builder and the stat line are all the real
 * ones. Ambush and Overwhelming Presence are read with THEIR ROWS WRITTEN BY HAND, on a player holding a Demonic whip,
 * because their tests need a skill to cast from a trap (Ambush) and a slot converted into a trap (Overwhelming Presence),
 * which no passive row grants. NOTHING WAITS ON A TICK: the world's clock is moved by hand. Helper types and functions are
 * named after the subject, the Saboteur's devices.
 */
namespace CataclysmSaboteurDeviceTest
{
	/** Centimetres in a metre. */
	constexpr float SaboteurDeviceMetre = 100.0f;

	/** A world for one test, destroyed when the test ends. */
	struct FSaboteurDeviceWorld
	{
		FSaboteurDeviceWorld() : World(CataclysmTestWorld::MakeWorldThatHasBegunPlay()) {}
		~FSaboteurDeviceWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
		UWorld* World = nullptr;
	};

	/** A possessed player character, its state, its equipment, its weapon slots and its ability system. */
	struct FSaboteurDevicePlayer
	{
		ACataclysmPlayerCharacter* Character = nullptr;
		ACataclysmPlayerState* State = nullptr;
		UCataclysmEquipmentComponent* Equipment = nullptr;
		UCataclysmWeaponSlotsComponent* Slots = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;

		bool IsComplete() const
		{
			return Character && State && Equipment && Slots && System;
		}

		/** A pool of `Maximum` Fervour, holding `Current`. */
		void Pool(float Maximum, float Current) const
		{
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(), Maximum);
			System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), Current);
		}

		/** The figure `evasion` asks for now, with no tags, as the defence step asks it with a blow's. */
		float Evasion() const
		{
			return System->StatForSkill(FName(TEXT("evasion")), FGameplayTagContainer(), 0.0f);
		}
	};

	/** A possessed player at `Where`, made as `FTrapRowRig` makes one: no creation choice, no refresh. */
	FSaboteurDevicePlayer SaboteurDeviceSpawnPlayer(UWorld* World, const FVector& Where)
	{
		FSaboteurDevicePlayer Made;
		ACataclysmPlayerState* PlayerState = World->SpawnActor<ACataclysmPlayerState>();
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		Made.Character = World->SpawnActor<ACataclysmPlayerCharacter>(Where, FRotator::ZeroRotator);
		if (!PlayerState || !Controller || !Made.Character)
		{
			return Made;
		}
		Controller->SetPlayerState(PlayerState);
		Controller->Possess(Made.Character);
		Made.State = Made.Character->GetPlayerState<ACataclysmPlayerState>();
		Made.Equipment = Made.Character->GetEquipment();
		Made.Slots = Made.Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>();
		Made.System = Made.State ? Made.State->GetCataclysmAbilitySystemComponent() : nullptr;
		return Made;
	}

	/**
	 * A level 10 War Greataxe player allocated `Points` in `Node` of the generated table and refreshed, with a pool of 100
	 * holding 50. False, with the command that rebuilds the assets, when the node has no row in the loaded table.
	 */
	bool SaboteurDeviceAllocated(FAutomationTestBase& Test, const FSaboteurDevicePlayer& Player, const TCHAR* Node,
								 int32 Points)
	{
		const UDataTable* Effects = UCataclysmPassiveTree::LoadEffectTable();
		if (!Test.TestNotNull(TEXT("set-up: the passive effect table loads"), Effects))
		{
			return false;
		}
		if (UCataclysmPassiveTree::EffectsFor(Effects, FName(Node)).Num() == 0)
		{
			Test.AddError(FString::Printf(TEXT("%s has no row in DT_PassiveEffects. If it has one in "
											   "game/Data/PassiveEffects.csv, the asset is older than the CSV: run  python "
											   "tools/run_editor_python.py tools/generate_datatable_assets.py"), Node));
			return false;
		}
		Player.State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));
		Player.State->SetLevelAndExperience(10, 0);
		if (!Test.TestTrue(TEXT("set-up: a War player reaches the Saboteur tree"),
						   Player.State->ReachableTrees().Contains(TEXT("Saboteur"))))
		{
			return false;
		}
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(Node), Points);
		Player.State->SetPassiveAllocation(Allocation, TArray<FName>());
		Player.Equipment->RefreshAttributes(Player.System);
		Player.Pool(100.0f, 50.0f);
		return true;
	}

	/** A modifier written by hand, as the item would build it from a row. */
	FCataclysmStatModifier SaboteurDeviceRow(float Value, ECataclysmStatBucket Bucket, const TCHAR* RequiredTag = nullptr,
											 ECataclysmStatCondition Condition = ECataclysmStatCondition::Always,
											 float ConditionValue = 0.0f)
	{
		FCataclysmStatModifier Row;
		Row.Bucket = Bucket;
		Row.Source = ECataclysmModifierSource::Enchantment;
		Row.Value = Value;
		Row.Condition = Condition;
		Row.ConditionValue = ConditionValue;
		if (RequiredTag)
		{
			Row.RequiredTags.AddTag(FGameplayTag::RequestGameplayTag(FName(RequiredTag), /*ErrorIfNotFound=*/false));
		}
		return Row;
	}

	/** A row and the stat it is on, as `SaboteurDeviceHolds` takes them. */
	TPair<FName, FCataclysmStatModifier> SaboteurDeviceOn(const TCHAR* Stat, const FCataclysmStatModifier& Row)
	{
		return TPair<FName, FCataclysmStatModifier>(FName(Stat), Row);
	}

	/** REPLACES THE WHOLE STAT LINE with these rows, each on its stat. */
	void SaboteurDeviceHolds(const FSaboteurDevicePlayer& Player,
							 const TArray<TPair<FName, FCataclysmStatModifier>>& Rows)
	{
		TMap<FName, FCataclysmStatInputs> Stats;
		for (const TPair<FName, FCataclysmStatModifier>& Each : Rows)
		{
			FCataclysmStatInputs& Line = Stats.FindOrAdd(Each.Key);
			Line.Base = 0.0f;
			Line.Modifiers.Add(Each.Value);
		}
		Player.System->SetStatInputs(MoveTemp(Stats));
	}

	/** A player holding a Demonic whip (Heavy Searing Lash, a 5 m cone) with 100 attack damage, made as the trap rows' rig. */
	bool SaboteurDeviceWhip(FAutomationTestBase& Test, const FSaboteurDevicePlayer& Player)
	{
		Player.Slots->SetDamageType(TEXT("Demonic"));
		Player.Slots->EquipWeaponType(TEXT("Whip"));
		const bool bHeavy = Player.Slots->GetAvailableSkills().FindByPredicate(
			[](const FCataclysmWeaponSkill& Skill) { return Skill.Slot == ECataclysmAbilitySlot::Heavy; }) != nullptr;
		if (!bHeavy)
		{
			Test.AddError(TEXT("set-up: a Demonic whip with a Heavy row. If it is missing, DT_WeaponSkills may be older than "
							   "game/Data/WeaponSkills.csv: run tools/generate_datatable_assets.py"));
			return false;
		}
		Player.System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
		Player.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
		Player.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
		Player.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
		Player.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
		return true;
	}

	/** A creature of the monsters' side with 50,000 health and nothing else. */
	ACataclysmEnemyCharacter* SaboteurDeviceEnemy(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		UAbilitySystemComponent* Its = Made ? Made->GetAbilitySystemComponent() : nullptr;
		if (!Its)
		{
			return nullptr;
		}
		Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
		Made->SetAttackDamage(0.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 50000.0f);
		Its->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 50000.0f);
		return Made;
	}

	/** The increases of trap damage a trap's blow on `Target` takes, as `ApplyHit` asks them, for `Trap`'s blow. */
	float SaboteurDeviceTrapIncreases(const FSaboteurDevicePlayer& Player, const AActor* Target, const AActor* Trap)
	{
		FGameplayTagContainer TrapTags;
		TrapTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap"))));
		float Increases = 0.0f;
		float More = 1.0f;
		Player.System->TrapDamageNamingTrap(TrapTags, Target, Increases, More, TWeakObjectPtr<const AActor>(Trap));
		return Increases;
	}
}

#define CATACLYSM_SABOTEUR_DEVICE_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Razor Instincts: one placing, one stack
// ---------------------------------------------------------------------------

CATACLYSM_SABOTEUR_DEVICE_TEST(FCataclysmSaboteurDeviceRazorInstinctsTest,
	"Cataclysm.SaboteurDevices.RazorInstinctsOnePlacingGrantsOneStackOfThree")
{
	using namespace CataclysmSaboteurDeviceTest;
	// THE GENERATED ROW, one point: 3% evasion, the row's own stack granted by `device_placed`, 3 seconds, at most one.
	// The coordinating session's ruling of 2026-11-25 asked for this measure: ONE PLACING GIVES 3, NOT 6. A trap placed
	// raises `trap_placed` and `device_placed`; only the second grants. THE CONTROL is the figure before anything is
	// placed. A gadget placed within the window adds nothing (the cap of one, its window restarted), and 3.1 seconds
	// after the gadget the stack has lapsed.
	FSaboteurDeviceWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	const FSaboteurDevicePlayer Player = SaboteurDeviceSpawnPlayer(Scope.World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player with its parts"), Player.IsComplete())
		|| !SaboteurDeviceAllocated(*this, Player, TEXT("Saboteur_basic_trunk_012"), 1))
	{
		return false;
	}
	const float Before = Player.Evasion();

	ACataclysmMinion* Trap = ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy,
															  FVector(3.0f * SaboteurDeviceMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a trap is placed"), Trap))
	{
		return false;
	}
	TestEqual(TEXT("one trap placed is one stack: 3% more evasion, not 6%"), Player.Evasion() - Before, 3.0f, 0.001f);

	ACataclysmMinion* Gadget = ACataclysmMinion::PlaceSkillGadget(Player.Character, ECataclysmAbilitySlot::Special,
																	FVector(0.0f, 3.0f * SaboteurDeviceMetre, 0.0f));
	if (TestNotNull(TEXT("set-up: a gadget is placed"), Gadget))
	{
		TestEqual(TEXT("a gadget placed within the window adds nothing: at most one stack"), Player.Evasion() - Before,
				  3.0f, 0.001f);
	}
	Scope.World->TimeSeconds += 3.1f;
	TestEqual(TEXT("and 3.1 seconds later the stack has lapsed"), Player.Evasion() - Before, 0.0f, 0.001f);
	return true;
}

CATACLYSM_SABOTEUR_DEVICE_TEST(FCataclysmSaboteurDeviceSharedKeyTest,
	"Cataclysm.SaboteurDevices.TwoRowsOnOneStackKeyEachCountTheOneStack")
{
	using namespace CataclysmSaboteurDeviceTest;
	// WHY RAZOR INSTINCTS IS ONE ROW ON ONE EVENT, MEASURED. Written by hand as two rows of one node would build: two
	// evasion rows of 3 under ONE stack key (the key is node and stat, `UCataclysmItemModifiers::OwnStackKeyFor`), one
	// granted on `trap_placed` and the other on `gadget_placed`. A single trap placed grants the key one stack, and BOTH
	// rows read it: 6. THE CONTROL is the figure before the trap.
	FSaboteurDeviceWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	const FSaboteurDevicePlayer Player = SaboteurDeviceSpawnPlayer(Scope.World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player with its parts"), Player.IsComplete()))
	{
		return false;
	}
	Player.Pool(100.0f, 50.0f);
	const FName Key(TEXT("Passive_Saboteur_basic_trunk_012:evasion"));
	FCataclysmStatModifier Row = SaboteurDeviceRow(3.0f, ECataclysmStatBucket::Flat);
	Row.Scale = ECataclysmStatScale::PerOwnStack;
	Row.ScaleStep = 1.0f;
	Row.StackKey = Key;
	SaboteurDeviceHolds(Player, {SaboteurDeviceOn(TEXT("evasion"), Row), SaboteurDeviceOn(TEXT("evasion"), Row)});
	TArray<FCataclysmPoolAction> Grants;
	for (const TCHAR* Event : {TEXT("trap_placed"), TEXT("gadget_placed")})
	{
		FCataclysmPoolAction Grant;
		Grant.Event = FName(Event);
		Grant.StackKey = Key;
		Grant.StackSeconds = 3.0f;
		Grant.StackCap = 1;
		Grant.TriggerKey = FName(*FString::Printf(TEXT("%s:%s"), *Key.ToString(), Event));
		Grants.Add(Grant);
	}
	Player.System->SetPoolActions(Grants);
	const float Before = Player.Evasion();

	ACataclysmMinion* Trap = ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy,
															  FVector(3.0f * SaboteurDeviceMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a trap is placed"), Trap))
	{
		return false;
	}
	TestEqual(TEXT("the key holds one stack"), Player.System->OwnStacksHeld(Key), 1);
	TestEqual(TEXT("and both rows read it: one trap placed is 6, which one row on one event avoids"),
			  Player.Evasion() - Before, 6.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// Sentry Network and Overcautious: the gadget's distance, and a gadget struck
// ---------------------------------------------------------------------------

CATACLYSM_SABOTEUR_DEVICE_TEST(FCataclysmSaboteurDeviceSentryNetworkTest,
	"Cataclysm.SaboteurDevices.SentryNetworkHoldsWithinFifteenMetresOfAGadget")
{
	using namespace CataclysmSaboteurDeviceTest;
	// THE GENERATED ROW, one point: 2% evasion under `own_gadget_within_metres` 15. THE CONTROLS: no gadget, and a gadget
	// 20 m away, each nothing; the same gadget moved to 5 m away gives 2.
	FSaboteurDeviceWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	const FSaboteurDevicePlayer Player = SaboteurDeviceSpawnPlayer(Scope.World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player with its parts"), Player.IsComplete())
		|| !SaboteurDeviceAllocated(*this, Player, TEXT("Saboteur_basic_gadget_mid_006"), 1))
	{
		return false;
	}
	const float Before = Player.Evasion();
	ACataclysmMinion* Gadget = ACataclysmMinion::PlaceSkillGadget(Player.Character, ECataclysmAbilitySlot::Special,
																	FVector(20.0f * SaboteurDeviceMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a gadget is placed"), Gadget))
	{
		return false;
	}
	TestEqual(TEXT("control: a gadget 20 m away is not within 15"), Player.Evasion() - Before, 0.0f, 0.001f);
	Gadget->SetActorLocation(FVector(5.0f * SaboteurDeviceMetre, 0.0f, 0.0f));
	TestEqual(TEXT("the same gadget 5 m away is: 2% more evasion"), Player.Evasion() - Before, 2.0f, 0.001f);
	return true;
}

CATACLYSM_SABOTEUR_DEVICE_TEST(FCataclysmSaboteurDeviceOvercautiousTest,
	"Cataclysm.SaboteurDevices.OvercautiousHoldsForTwoSecondsAfterAGadgetIsStruck")
{
	using namespace CataclysmSaboteurDeviceTest;
	// THE GENERATED ROW, one point: 4% evasion under `seconds_after_gadget_struck` 2 ("under attack", the coordinating
	// session's ruling). THE CONTROLS: a gadget standing and not struck gives nothing, and a tick on it gives nothing; a
	// landed blow on it gives 4, still held 1.9 seconds on and gone at 2.1.
	FSaboteurDeviceWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	const FSaboteurDevicePlayer Player = SaboteurDeviceSpawnPlayer(Scope.World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player with its parts"), Player.IsComplete())
		|| !SaboteurDeviceAllocated(*this, Player, TEXT("Saboteur_basic_gadget_mid_002"), 1))
	{
		return false;
	}
	ACataclysmEnemyCharacter* Attacker = SaboteurDeviceEnemy(Scope.World, FVector(0.0f, 10.0f * SaboteurDeviceMetre, 0.0f));
	ACataclysmMinion* Gadget = ACataclysmMinion::PlaceSkillGadget(Player.Character, ECataclysmAbilitySlot::Special,
																	FVector(3.0f * SaboteurDeviceMetre, 0.0f, 0.0f));
	UCataclysmAbilitySystemComponent* Its =
		Gadget ? Cast<UCataclysmAbilitySystemComponent>(UCataclysmTargeting::AbilitySystemOf(Gadget)) : nullptr;
	if (!TestNotNull(TEXT("set-up: an attacker"), Attacker) || !TestNotNull(TEXT("set-up: a gadget and its ability system"), Its))
	{
		return false;
	}
	const float Before = Player.Evasion();
	TestEqual(TEXT("control: a gadget standing and not struck gives nothing"), Player.Evasion() - Before, 0.0f, 0.001f);
	Its->NoteHitTaken(/*bLanded=*/true, Attacker, /*bDamageOverTime=*/true);
	TestEqual(TEXT("control: a tick on the gadget is not an attack"), Player.Evasion() - Before, 0.0f, 0.001f);
	Its->NoteHitTaken(/*bLanded=*/true, Attacker, /*bDamageOverTime=*/false);
	TestEqual(TEXT("a landed blow on the gadget: 4% more evasion"), Player.Evasion() - Before, 4.0f, 0.001f);
	Scope.World->TimeSeconds += 1.9f;
	TestEqual(TEXT("still held 1.9 seconds on"), Player.Evasion() - Before, 4.0f, 0.001f);
	Scope.World->TimeSeconds += 0.2f;
	TestEqual(TEXT("and gone at 2.1"), Player.Evasion() - Before, 0.0f, 0.001f);
	return true;
}

// ---------------------------------------------------------------------------
// Ambush: an enemy's first trap, and no later one
// ---------------------------------------------------------------------------

CATACLYSM_SABOTEUR_DEVICE_TEST(FCataclysmSaboteurDeviceAmbushTest,
	"Cataclysm.SaboteurDevices.AmbushCountsAnEnemysFirstTrapAndNoLaterOne")
{
	using namespace CataclysmSaboteurDeviceTest;
	// AMBUSH'S ROW BY HAND: 5% increased `minion_damage` on `Type.Trap` under `target_not_yet_triggered_your_trap`, read as
	// `ApplyHit` reads a trap's blow (`TrapDamageNamingTrap`, naming the blow's trap). Two traps, A and B, and two
	// enemies. THE CONTROL: before anything is set off, A's blow on the first enemy takes the 5%. Then the first enemy
	// sets A off FOR REAL (`TriggerSkillTrap`, which writes the record): A's own blows on it still take the 5% (the
	// ruling: its first trap counts as not yet triggered), B's blow on it takes nothing, and B's blow on the second enemy,
	// which set nothing off, still takes the 5%.
	FSaboteurDeviceWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	const FSaboteurDevicePlayer Player = SaboteurDeviceSpawnPlayer(Scope.World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player with its parts"), Player.IsComplete())
		|| !SaboteurDeviceWhip(*this, Player))
	{
		return false;
	}
	Player.Pool(100.0f, 50.0f);
	SaboteurDeviceHolds(Player, {SaboteurDeviceOn(TEXT("minion_damage"),
												  SaboteurDeviceRow(5.0f, ECataclysmStatBucket::Increased, TEXT("Type.Trap"),
																	ECataclysmStatCondition::TargetNotYetTriggeredYourTrap))});
	ACataclysmMinion* TrapA = ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy,
															   FVector(10.0f * SaboteurDeviceMetre, 0.0f, 0.0f));
	ACataclysmMinion* TrapB = ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy,
															   FVector(-10.0f * SaboteurDeviceMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* First = SaboteurDeviceEnemy(Scope.World, FVector(12.0f * SaboteurDeviceMetre, 0.0f, 0.0f));
	ACataclysmEnemyCharacter* Second = SaboteurDeviceEnemy(Scope.World, FVector(0.0f, 30.0f * SaboteurDeviceMetre, 0.0f));
	if (!TestNotNull(TEXT("set-up: trap A"), TrapA) || !TestNotNull(TEXT("set-up: trap B"), TrapB)
		|| !TestNotNull(TEXT("set-up: the first enemy"), First) || !TestNotNull(TEXT("set-up: the second enemy"), Second))
	{
		return false;
	}

	TestEqual(TEXT("control: before anything is set off, A's blow on the first enemy takes 5%"),
			  SaboteurDeviceTrapIncreases(Player, First, TrapA), 0.05f, 0.0001f);
	if (!TestTrue(TEXT("set-up: the first enemy sets trap A off, and its cast starts"), TrapA->TriggerSkillTrap(First)))
	{
		return false;
	}
	TestEqual(TEXT("A's own blow on the enemy that set it off still takes 5%: its first trap"),
			  SaboteurDeviceTrapIncreases(Player, First, TrapA), 0.05f, 0.0001f);
	TestEqual(TEXT("B's blow on that enemy takes nothing: it has set off one of your traps"),
			  SaboteurDeviceTrapIncreases(Player, First, TrapB), 0.0f, 0.0001f);
	TestEqual(TEXT("B's blow on the second enemy, which set nothing off, still takes 5%"),
			  SaboteurDeviceTrapIncreases(Player, Second, TrapB), 0.05f, 0.0001f);
	return true;
}

// ---------------------------------------------------------------------------
// Overwhelming Presence: no room for another trap
// ---------------------------------------------------------------------------

CATACLYSM_SABOTEUR_DEVICE_TEST(FCataclysmSaboteurDeviceOverwhelmingPresenceTest,
	"Cataclysm.SaboteurDevices.OverwhelmingPresenceHoldsOnlyWhenNoTrapSlotHasRoom")
{
	using namespace CataclysmSaboteurDeviceTest;
	// THE NODE'S TWO ROWS BY HAND, each under `no_room_for_a_trap`: 50% more trap damage and 100% more trap trigger radius,
	// with 50% increased trigger radius beside them, so "doubled" is seen to double the radius the increase makes (300 cm
	// x 1.5 = 450, doubled 900). A trap reserves 5. THE CONTROLS: with the Heavy slot a trap and a pool of 100 there is
	// room, so 450 and a "more" of 1; with a pool of 4 and NO slot a trap the condition refuses too (no trap to place), so
	// 450 again. With the Heavy slot a trap and a pool of 4, no room: 900 and 1.5.
	FSaboteurDeviceWorld Scope;
	if (!TestNotNull(TEXT("a world"), Scope.World))
	{
		return false;
	}
	const FSaboteurDevicePlayer Player = SaboteurDeviceSpawnPlayer(Scope.World, FVector::ZeroVector);
	if (!TestTrue(TEXT("set-up: a possessed player with its parts"), Player.IsComplete())
		|| !SaboteurDeviceWhip(*this, Player))
	{
		return false;
	}
	const FName Radius(ACataclysmMinion::TrapTriggerRadiusStat);
	const FName Damage(TEXT("minion_damage"));
	const TPair<FName, FCataclysmStatModifier> Doubled(
		Radius, SaboteurDeviceRow(100.0f, ECataclysmStatBucket::More, nullptr, ECataclysmStatCondition::NoRoomForATrap));
	const TPair<FName, FCataclysmStatModifier> Wider(Radius, SaboteurDeviceRow(50.0f, ECataclysmStatBucket::Increased));
	const TPair<FName, FCataclysmStatModifier> Harder(
		Damage, SaboteurDeviceRow(50.0f, ECataclysmStatBucket::More, TEXT("Type.Trap"), ECataclysmStatCondition::NoRoomForATrap));
	const TPair<FName, FCataclysmStatModifier> HeavyIsATrap(
		FName(UCataclysmAbilitySystemComponent::SkillSlotIsATrapStat),
		SaboteurDeviceRow(1.0f, ECataclysmStatBucket::Flat, TEXT("Slot.Heavy")));
	FGameplayTagContainer TrapTags;
	TrapTags.AddTag(FGameplayTag::RequestGameplayTag(FName(TEXT("Type.Trap"))));
	const auto TrapMore = [&Player, &TrapTags]()
	{
		float Increases = 0.0f;
		float More = 1.0f;
		Player.System->TrapDamageNamingTrap(TrapTags, /*Target=*/nullptr, Increases, More);
		return More;
	};
	const float Base = ACataclysmMinion::SkillTrapTriggerRadiusCm;

	SaboteurDeviceHolds(Player, {HeavyIsATrap, Doubled, Wider, Harder});
	if (!TestTrue(TEXT("set-up: the Heavy slot is a trap"), Player.System->SlotIsASkillTrap(ECataclysmAbilitySlot::Heavy)))
	{
		return false;
	}
	Player.Pool(100.0f, 50.0f);
	TestFalse(TEXT("control: with a pool of 100 there is room for a trap"), Player.System->HasNoRoomForATrap());
	TestEqual(TEXT("control: so the radius is the increase's alone, 450 cm"),
			  ACataclysmMinion::SkillTrapTriggerRadiusCmFor(Player.Character, TrapTags), Base * 1.5f, 0.01f);
	TestEqual(TEXT("control: and trap damage takes no \"more\""), TrapMore(), 1.0f, 0.0001f);

	Player.Pool(4.0f, 4.0f);
	TestTrue(TEXT("with a pool of 4 a trap of 5 has no room"), Player.System->HasNoRoomForATrap());
	TestEqual(TEXT("so the radius is doubled: 900 cm"),
			  ACataclysmMinion::SkillTrapTriggerRadiusCmFor(Player.Character, TrapTags), Base * 3.0f, 0.01f);
	TestEqual(TEXT("and trap damage takes 50% more"), TrapMore(), 1.5f, 0.0001f);

	SaboteurDeviceHolds(Player, {Doubled, Wider, Harder});
	TestFalse(TEXT("control: with no slot a trap there is no trap to place, so no \"no room\""),
			  Player.System->HasNoRoomForATrap());
	TestEqual(TEXT("control: and the radius is 450 cm again"),
			  ACataclysmMinion::SkillTrapTriggerRadiusCmFor(Player.Character, TrapTags), Base * 1.5f, 0.01f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
