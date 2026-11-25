// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmCombatEvents.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmTriggeredSkill.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "AbilitySystem/CataclysmWeaponSkills.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Items/CataclysmWeaponSlotsComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

/**
 * THE WAR VOW OPTIONS' FOURTH SLICE, SECOND HALF, V4b, 2026-11-25, on the coordinating session's rulings on the
 * Saboteur's B options that day: Counterfire's answers to an evaded blow, and Magnum Opus's eldest gadget casting every
 * skill used. War machines (Bolt Turrets) stand in for the devices, spawned as `CataclysmAoeStrikesAlliesTests.cpp`
 * spawns them, and the skill is The Gathering on a Demonic Whip, the rig of `Cataclysm.GadgetCopiesSkill.`.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV4bTest
{
	constexpr float WarVowsV4bMetre = 100.0f;

	/** A possessed player holding a Demonic Whip, with an attack damage of 100 and a pool of mana. */
	struct FWarVowsV4bPlayer
	{
		explicit FWarVowsV4bPlayer(UWorld* World)
		{
			ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			Character = World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			if (!State || !Controller || !Character)
			{
				return;
			}
			Controller->SetPlayerState(State);
			Controller->Possess(Character);
			System = State->GetCataclysmAbilitySystemComponent();
			if (UCataclysmWeaponSlotsComponent* Slots = Character->FindComponentByClass<UCataclysmWeaponSlotsComponent>())
			{
				Slots->SetDamageType(TEXT("Demonic"));
				Slots->EquipWeaponType(TEXT("Whip"));
			}
			if (System)
			{
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
				System->SetNumericAttributeBase(UCataclysmCombatAttributeSet::GetAttackDamageAttribute(), 100.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxManaAttribute(), 1000.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetManaAttribute(), 1000.0f);
			}
		}
		bool IsUsable() const { return Character && System; }

		/** Magnum Opus's flag, or nothing. Replaces the whole stat line. */
		void HoldsMagnumOpus(bool bHolds) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			if (bHolds)
			{
				FCataclysmStatModifier Flag;
				Flag.Bucket = ECataclysmStatBucket::Flat;
				Flag.Source = ECataclysmModifierSource::PassiveKeystone;
				Flag.Value = 1.0f;
				FCataclysmStatInputs& Line = Stats.FindOrAdd(FName(ACataclysmMinion::EldestGadgetCastsEverySkillStat));
				Line.Modifiers.Add(Flag);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A creature on the monsters' side at `Where`, with 100,000 health. */
	ACataclysmEnemyCharacter* WarVowsV4bCreatureAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(100000.0f);
		}
		return Made;
	}

	float WarVowsV4bHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	ACataclysmMinion* WarVowsV4bTurretAt(AActor* Player, const FVector& Where)
	{
		return ACataclysmMinion::Spawn(Player, Where, /*Lifetime=*/600.0f, /*bBurns=*/false, TEXT("BoltTurret"));
	}
}

#define CATACLYSM_WAR_VOWS_V4B_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Counterfire
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V4B_TEST(FCataclysmWarVowsV4bCounterfireTest,
	"Cataclysm.WarVowsV4b.AnEvadedBlowIsAnsweredByOneDeviceForEveryTwoTheNearestFirstOnceASecond")
{
	using namespace CataclysmWarVowsV4bTest;
	// A PLAYER HOLDING COUNTERFIRE'S ACTION (one answer for every 2 devices, 100% of a shot, once a second) and four
	// Bolt Turrets of its own, 5, 6, 30 and 40 metres east. An attacker stands at the origin. THE MEASURE: one turret's
	// ordinary blow on a separate creature. An evaded blow from the attacker is answered by two turrets: the attacker
	// loses two blows. A second evade at once is answered by none (THE FIRST CONTROL, the second); one 1.1 seconds later,
	// by two again. With three turrets gone, one stands, and an evade is answered by none (THE SECOND CONTROL: one for
	// every two, rounded down).
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV4bPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const FVector East(WarVowsV4bMetre, 0.0f, 0.0f);
	ACataclysmMinion* Turrets[] = {WarVowsV4bTurretAt(Player.Character, East * 5.0f),
								   WarVowsV4bTurretAt(Player.Character, East * 6.0f),
								   WarVowsV4bTurretAt(Player.Character, East * 30.0f),
								   WarVowsV4bTurretAt(Player.Character, East * 40.0f)};
	ACataclysmEnemyCharacter* Attacker = WarVowsV4bCreatureAt(World, FVector(0.0f, 50.0f * WarVowsV4bMetre, 0.0f));
	ACataclysmEnemyCharacter* Measure = WarVowsV4bCreatureAt(World, FVector(0.0f, -50.0f * WarVowsV4bMetre, 0.0f));
	for (const ACataclysmMinion* Each : Turrets)
	{
		if (!TestNotNull(TEXT("set-up: a turret"), Each))
		{
			return false;
		}
	}
	if (!TestTrue(TEXT("set-up: an attacker and a measure"), Attacker && Measure))
	{
		return false;
	}
	FCataclysmPoolAction Counterfire;
	Counterfire.Event = FName(TEXT("dodge"));
	Counterfire.Percent = 100.0f;
	Counterfire.EventValue = 2.0f;
	Counterfire.TriggerCooldownSeconds = 1.0f;
	Counterfire.bCounterfire = true;
	Counterfire.TriggerKey = FName(TEXT("WarVowsV4b:counterfire"));
	Player.System->SetPoolActions({Counterfire});

	const float MeasureBefore = WarVowsV4bHealthOf(Measure);
	Turrets[2]->AttackTarget(Measure);
	const float OneBlow = MeasureBefore - WarVowsV4bHealthOf(Measure);
	if (!TestTrue(*FString::Printf(TEXT("set-up: one turret's blow takes something: %.2f"), OneBlow), OneBlow > 0.0f))
	{
		return false;
	}

	const auto Evade = [&]()
	{
		const float Before = WarVowsV4bHealthOf(Attacker);
		Player.System->NoteEvaded(Attacker, /*bMelee=*/true);
		return Before - WarVowsV4bHealthOf(Attacker);
	};
	TestEqual(TEXT("an evaded blow is answered by two of the four: two blows"), Evade(), 2.0f * OneBlow, 0.5f);
	TestEqual(TEXT("control: a second evade at once is answered by none"), Evade(), 0.0f, 0.01f);
	World->TimeSeconds += 1.1f;
	TestEqual(TEXT("and one 1.1 seconds later by two again"), Evade(), 2.0f * OneBlow, 0.5f);

	for (int32 Index = 1; Index < 4; ++Index)
	{
		Turrets[Index]->Destroy();
	}
	World->TimeSeconds += 1.1f;
	TestEqual(TEXT("control: with one device standing, none answers: one for every two, rounded down"), Evade(), 0.0f,
			  0.01f);
	return true;
}

// ---------------------------------------------------------------------------
// Magnum Opus
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V4B_TEST(FCataclysmWarVowsV4bMagnumOpusTest,
	"Cataclysm.WarVowsV4b.TheEldestGadgetCastsEverySkillUsedAndTheNextTakesItsPlace")
{
	using namespace CataclysmWarVowsV4bTest;
	// A PLAYER HOLDING A DEMONIC WHIP AND TWO BOLT TURRETS: the elder placed first, 20 m north, the younger 20 m south, a
	// creature 3 m from each. The whip's Ultimate, The Gathering, is announced as used by hand.
	// THE CONTROLS: without Magnum Opus no turret casts; with it, a use from the movement slot is not copied. With it, a
	// use is copied once by the elder alone, on the next tick. With the elder gone, the younger copies the next use.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV4bPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	FCataclysmWeaponSkill Gathering;
	if (!TestTrue(TEXT("set-up: the player holds The Gathering. If not, DT_WeaponSkills may be older than "
					   "game/Data/WeaponSkills.csv: run tools/generate_datatable_assets.py"),
				  UCataclysmTriggeredSkill::HeldSkillNamed(Player.Character, FName(TEXT("The Gathering")), Gathering))
		|| !TestTrue(TEXT("set-up: a skill a row may cast again"), UCataclysmTriggeredSkill::RepeatsFromARow(Gathering)))
	{
		return false;
	}
	const FVector North(0.0f, 20.0f * WarVowsV4bMetre, 0.0f);
	ACataclysmMinion* Elder = WarVowsV4bTurretAt(Player.Character, North);
	ACataclysmMinion* Younger = WarVowsV4bTurretAt(Player.Character, -North);
	ACataclysmEnemyCharacter* ByTheElder = WarVowsV4bCreatureAt(World, North + FVector(3.0f * WarVowsV4bMetre, 0, 0));
	ACataclysmEnemyCharacter* ByTheYounger =
		WarVowsV4bCreatureAt(World, -North + FVector(3.0f * WarVowsV4bMetre, 0, 0));
	if (!TestTrue(TEXT("set-up: two turrets and two creatures"), Elder && Younger && ByTheElder && ByTheYounger)
		|| !TestTrue(TEXT("set-up: the elder is the eldest"), ACataclysmMinion::EldestGadgetOf(Player.Character) == Elder)
		|| !TestTrue(TEXT("set-up: the elder's target is the creature beside it"), Elder->SkillCopyTarget() == ByTheElder))
	{
		return false;
	}
	const auto Use = [&](ECataclysmAbilitySlot Slot)
	{
		const FVector Aim = ByTheElder->GetActorLocation();
		UCataclysmCombatEvents::NoteSkillUsed(Player.Character, Gathering.Name, Gathering.Tags, Slot, &Aim,
											  /*bHasCooldown=*/true);
		++GFrameCounter;  // a hand-made tick in a new frame (RunClock)
		World->GetTimerManager().Tick(0.1f);
	};

	Player.HoldsMagnumOpus(false);
	Use(Gathering.Slot);
	TestEqual(TEXT("control: without Magnum Opus the elder casts nothing"), Elder->SkillCopiesMade, 0);

	Player.HoldsMagnumOpus(true);
	Use(ECataclysmAbilitySlot::Movement);
	TestEqual(TEXT("control: a use from the movement slot is not copied"), Elder->SkillCopiesMade, 0);

	Use(Gathering.Slot);
	TestEqual(TEXT("with it, the elder copies the use once"), Elder->SkillCopiesMade, 1);
	TestEqual(TEXT("and the younger not at all"), Younger->SkillCopiesMade, 0);

	Elder->Destroy();
	TestTrue(TEXT("with the elder gone, the younger is the eldest"),
			 ACataclysmMinion::EldestGadgetOf(Player.Character) == Younger);
	Use(Gathering.Slot);
	TestEqual(TEXT("and it copies the next use"), Younger->SkillCopiesMade, 1);
	return true;
}

// ---------------------------------------------------------------------------
// The options, handed to a War player
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V4B_TEST(FCataclysmWarVowsV4bHandedOverTest,
	"Cataclysm.WarVowsV4b.AWarPlayerTakingTheOptionsIsHandedCounterfireAndMagnumOpus")
{
	// A LEVEL 100 WAR PLAYER THROUGH `SetPassiveAllocation`: Counterfire hands over one action (on `dodge`, one for every
	// 2, 100%, once a second), and Magnum Opus its flag. THE CONTROLS: the first options of the same capstones hand over
	// neither.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACataclysmPlayerState* State = World->SpawnActor<ACataclysmPlayerState>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACataclysmPlayerCharacter* Character =
		World->SpawnActor<ACataclysmPlayerCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestTrue(TEXT("set-up: a player"), State && Controller && Character))
	{
		return false;
	}
	Controller->SetPlayerState(State);
	Controller->Possess(Character);
	State->SetCreationChoice(FName(TEXT("Greataxe")), FName(TEXT("War")));
	State->SetLevelAndExperience(100, 0);
	UCataclysmAbilitySystemComponent* System = State->GetCataclysmAbilitySystemComponent();
	UCataclysmEquipmentComponent* Equipment = Character->GetEquipment();
	if (!TestTrue(TEXT("set-up: its ability system and equipment"), System && Equipment))
	{
		return false;
	}
	const auto Take = [&](int32 ThirdOath, int32 FinalOath)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(TEXT("Saboteur_capstone_100")), 1);
		Allocation.SetChosenOption(FName(TEXT("Saboteur_capstone_100")), ThirdOath);
		Allocation.Add(FName(TEXT("Saboteur_capstone_200")), 1);
		Allocation.SetChosenOption(FName(TEXT("Saboteur_capstone_200")), FinalOath);
		State->SetPassiveAllocation(Allocation, TArray<FName>());
		Equipment->RefreshAttributes(System);
	};
	const auto Counterfires = [System]()
	{
		TArray<FCataclysmPoolAction> Found;
		for (const FCataclysmPoolAction& Action : System->GetPoolActions())
		{
			if (Action.bCounterfire)
			{
				Found.Add(Action);
			}
		}
		return Found;
	};

	Take(1, 1);
	TestEqual(TEXT("control: the first options hand over no Counterfire"), Counterfires().Num(), 0);
	TestFalse(TEXT("control: and no Magnum Opus"), ACataclysmMinion::EldestGadgetCastsEverySkillFor(Character));

	Take(3, 2);
	const TArray<FCataclysmPoolAction> Found = Counterfires();
	if (TestEqual(TEXT("Counterfire hands over one action"), Found.Num(), 1))
	{
		TestEqual(TEXT("on dodge"), Found[0].Event, FName(TEXT("dodge")));
		TestEqual(TEXT("one for every 2"), Found[0].EventValue, 2.0f, 0.001f);
		TestEqual(TEXT("at 100% of a shot"), Found[0].Percent, 100.0f, 0.001f);
		TestEqual(TEXT("once a second"), Found[0].TriggerCooldownSeconds, 1.0f, 0.001f);
	}
	TestTrue(TEXT("and Magnum Opus its flag"), ACataclysmMinion::EldestGadgetCastsEverySkillFor(Character));
	return true;
}

#undef CATACLYSM_WAR_VOWS_V4B_TEST

#endif // WITH_AUTOMATION_TESTS
