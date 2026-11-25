// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
#include "AbilitySystem/CataclysmCombatAttributeSet.h"
#include "AbilitySystem/CataclysmMinion.h"
#include "AbilitySystem/CataclysmSkillEffects.h"
#include "AbilitySystem/CataclysmStatPipeline.h"
#include "AbilitySystem/CataclysmTargeting.h"
#include "AbilitySystem/CataclysmTeams.h"
#include "AbilitySystem/CataclysmVitalAttributeSet.h"
#include "Character/CataclysmEnemyCharacter.h"
#include "Character/CataclysmPassiveTree.h"
#include "Character/CataclysmPlayerCharacter.h"
#include "Items/CataclysmEquipmentComponent.h"
#include "Player/CataclysmPlayerState.h"
#include "Tests/CataclysmTestWorld.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

/**
 * THE WAR VOW OPTIONS' FIFTH SLICE, FIRST HALF, V5a, 2026-11-25, on the coordinating session's rulings on the
 * Saboteur's C options that day: Creeping Mines, Where They Fall, Crossfire and Lightning Rod. War machines stand in for
 * devices where a skill would have to be cast, as `CataclysmWarVowsV4bTests.cpp` uses them.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV5aTest
{
	constexpr float WarVowsV5aMetre = 100.0f;

	/** One flat stat line for `SetStatInputs`. */
	struct FWarVowsV5aStat
	{
		const TCHAR* Stat;
		float Value;
	};

	/** A possessed player at the origin with a pool of 100 holding 100, every slot a trap's. */
	struct FWarVowsV5aPlayer
	{
		explicit FWarVowsV5aPlayer(UWorld* World)
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
			if (System)
			{
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetMaxClassResourceAttribute(),
												100.0f);
				System->SetNumericAttributeBase(UCataclysmClassResourceAttributeSet::GetClassResourceAttribute(), 100.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetMaxHealthAttribute(), 10000.0f);
				System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
				Holds({});
			}
		}
		bool IsUsable() const { return Character && System; }

		/** The trap flag and these flat stats. Replaces the whole stat line. */
		void Holds(const TArray<FWarVowsV5aStat>& Rows) const
		{
			TMap<FName, FCataclysmStatInputs> Stats;
			TArray<FWarVowsV5aStat> All = Rows;
			All.Add({TEXT("skill_slot_is_a_trap"), 1.0f});
			for (const FWarVowsV5aStat& Each : All)
			{
				FCataclysmStatModifier Row;
				Row.Bucket = ECataclysmStatBucket::Flat;
				Row.Source = ECataclysmModifierSource::PassiveKeystone;
				Row.Value = Each.Value;
				Stats.FindOrAdd(FName(Each.Stat)).Modifiers.Add(Row);
			}
			System->SetStatInputs(MoveTemp(Stats));
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	/** A creature on the monsters' side at `Where`, with 100,000 health and no defences. */
	ACataclysmEnemyCharacter* WarVowsV5aCreatureAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(100000.0f);
			Made->SetArmour(0.0f);
		}
		return Made;
	}

	float WarVowsV5aHealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	ACataclysmMinion* WarVowsV5aMachineAt(AActor* Player, const FVector& Where, const TCHAR* Type)
	{
		return ACataclysmMinion::Spawn(Player, Where, /*Lifetime=*/600.0f, /*bBurns=*/false, Type);
	}

	/** Pins Lightning Rod's roll for one test and puts it back. */
	struct FWarVowsV5aPinnedRoll
	{
		explicit FWarVowsV5aPinnedRoll(float Roll)
			: Variable(IConsoleManager::Get().FindConsoleVariable(TEXT("Cataclysm.LightningRodRoll")))
		{
			if (Variable)
			{
				Variable->Set(Roll, ECVF_SetByConsole);
			}
		}
		~FWarVowsV5aPinnedRoll()
		{
			if (Variable)
			{
				Variable->Set(-1.0f, ECVF_SetByConsole);
			}
		}
		IConsoleVariable* Variable = nullptr;
	};
}

#define CATACLYSM_WAR_VOWS_V5A_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

// ---------------------------------------------------------------------------
// Creeping Mines
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V5A_TEST(FCataclysmWarVowsV5aCreepTest,
	"Cataclysm.WarVowsV5a.AnArmedTrapCreepsTowardTheNearestEnemyWithinTenMetres")
{
	using namespace CataclysmWarVowsV5aTest;
	// A WAR SPIKE TRAP 20 m EAST OF THE PLAYER, a creature 8 m beyond it, outside its 1.5 m reach. THE FIRST CONTROL:
	// without the stats a step moves nothing. With them (3 m a second, within 10 m), one quarter-second step moves the trap
	// 75 cm toward the creature. THE SECOND CONTROL: a creature 12 m away, with the first gone, draws nothing. And a skill
	// trap with its arming time still running does not creep (THE THIRD CONTROL), while one armed at once does.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5aPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const FVector East(WarVowsV5aMetre, 0.0f, 0.0f);
	ACataclysmMinion* Spike = WarVowsV5aMachineAt(Player.Character, East * 20.0f, TEXT("SpikeTrap"));
	ACataclysmEnemyCharacter* Near = WarVowsV5aCreatureAt(World, East * 28.0f);
	if (!TestNotNull(TEXT("set-up: a Spike Trap"), Spike) || !TestNotNull(TEXT("set-up: a creature"), Near)
		|| !TestTrue(TEXT("set-up: the Spike Trap is a trap"), Spike->IsTrap()))
	{
		return false;
	}
	const FVector Start = Spike->GetActorLocation();
	ACataclysmMinion::GadgetStep(Spike);
	TestEqual(TEXT("control: without the stats a step moves nothing"),
			  static_cast<float>(FVector::Dist2D(Spike->GetActorLocation(), Start)), 0.0f, 0.01f);

	Player.Holds({{ACataclysmMinion::TrapCreepMetresPerSecondStat, 3.0f},
				  {ACataclysmMinion::TrapCreepReachMetresStat, 10.0f}});
	ACataclysmMinion::GadgetStep(Spike);
	TestEqual(TEXT("with them, one step moves it 75 cm"), static_cast<float>(FVector::Dist2D(Spike->GetActorLocation(), Start)), 75.0f, 0.5f);
	TestTrue(TEXT("toward the creature"), Spike->GetActorLocation().X > Start.X);

	Near->Destroy();
	WarVowsV5aCreatureAt(World, Spike->GetActorLocation() + East * 12.0f);
	const FVector Held = Spike->GetActorLocation();
	ACataclysmMinion::GadgetStep(Spike);
	TestEqual(TEXT("control: a creature 12 m away draws nothing"), static_cast<float>(FVector::Dist2D(Spike->GetActorLocation(), Held)), 0.0f,
			  0.01f);

	ACataclysmMinion* Slow = ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy,
															   FVector(0.0f, 40.0f * WarVowsV5aMetre, 0.0f));
	WarVowsV5aCreatureAt(World, FVector(6.0f * WarVowsV5aMetre, 40.0f * WarVowsV5aMetre, 0.0f));
	if (!TestNotNull(TEXT("set-up: a skill trap"), Slow) || !TestFalse(TEXT("set-up: not armed yet"), Slow->bSkillTrapArmed))
	{
		return false;
	}
	const FVector SlowStart = Slow->GetActorLocation();
	ACataclysmMinion::GadgetStep(Slow);
	TestEqual(TEXT("control: an unarmed skill trap does not creep"),
			  static_cast<float>(FVector::Dist2D(Slow->GetActorLocation(), SlowStart)), 0.0f, 0.01f);
	Slow->BeginSkillTrapArming(0.0f);
	ACataclysmMinion::GadgetStep(Slow);
	TestEqual(TEXT("and armed, it creeps 75 cm"), static_cast<float>(FVector::Dist2D(Slow->GetActorLocation(), SlowStart)), 75.0f, 0.5f);
	return true;
}

// ---------------------------------------------------------------------------
// Where They Fall
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V5A_TEST(FCataclysmWarVowsV5aWhereTheyFallTest,
	"Cataclysm.WarVowsV5a.AKillNearADeviceLeavesAnArmedFourSecondCopyOfTheLastTrap")
{
	using namespace CataclysmWarVowsV5aTest;
	// A PLAYER HOLDING WHERE THEY FALL'S ACTION (4 s, within 10 m) places a Heavy trap 3 m east. THE FIRST CONTROL: a kill
	// 30 m from every device leaves nothing. A kill 8 m from the trap leaves a copy where the creature stood, on the next
	// tick: from the Heavy slot, armed, lasting 4 s, reserving 1 Fervour.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5aPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	FCataclysmPoolAction Fall;
	Fall.Event = FName(TEXT("kill"));
	Fall.Percent = 4.0f;
	Fall.EventValue = 10.0f;
	Fall.bCopyLastTrapAtKill = true;
	Fall.TriggerKey = FName(TEXT("WarVowsV5a:fall"));
	Player.System->SetPoolActions({Fall});
	const FVector East(WarVowsV5aMetre, 0.0f, 0.0f);
	const ACataclysmMinion* First =
		ACataclysmMinion::PlaceSkillTrap(Player.Character, ECataclysmAbilitySlot::Heavy, East * 3.0f);
	ACataclysmEnemyCharacter* FarAway = WarVowsV5aCreatureAt(World, FVector(0.0f, 30.0f * WarVowsV5aMetre, 0.0f));
	ACataclysmEnemyCharacter* Beside = WarVowsV5aCreatureAt(World, East * 11.0f);
	if (!TestNotNull(TEXT("set-up: a trap"), First) || !TestTrue(TEXT("set-up: two creatures"), FarAway && Beside))
	{
		return false;
	}

	Player.System->ActOnEvent(FName(TEXT("kill")), nullptr, /*EventAmount=*/0.0f, /*bLanded=*/true, FarAway);
	World->GetTimerManager().Tick(0.1f);
	TestEqual(TEXT("control: a kill 30 m from every device leaves nothing"),
			  ACataclysmMinion::SkillTrapsOf(Player.Character).Num(), 1);

	World->TimeSeconds += 1.0f;
	Player.System->ActOnEvent(FName(TEXT("kill")), nullptr, /*EventAmount=*/0.0f, /*bLanded=*/true, Beside);
	World->GetTimerManager().Tick(0.1f);
	const TArray<ACataclysmMinion*> Traps = ACataclysmMinion::SkillTrapsOf(Player.Character);
	if (TestEqual(TEXT("a kill 8 m from the trap leaves a copy"), Traps.Num(), 2))
	{
		const ACataclysmMinion* Copy = Traps[0] == First ? Traps[1] : Traps[0];
		TestTrue(TEXT("where the creature stood"), FVector::Dist2D(Copy->GetActorLocation(), Beside->GetActorLocation()) < 1.0f);
		TestTrue(TEXT("from the Heavy slot"), Copy->SkillTrapSlot == ECataclysmAbilitySlot::Heavy);
		TestTrue(TEXT("armed at once"), Copy->bSkillTrapArmed);
		TestEqual(TEXT("lasting 4 seconds"), Copy->GetLifeSpan(), 4.0f, 0.05f);
		TestEqual(TEXT("reserving 1 Fervour"), Copy->ReservedFervour, 1.0f, 0.001f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Crossfire
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V5A_TEST(FCataclysmWarVowsV5aCrossfireTest,
	"Cataclysm.WarVowsV5a.AHitIsRepeatedFromUpToFifteenDevicesWithinFifteenMetresAtATenth")
{
	using namespace CataclysmWarVowsV5aTest;
	// A PLAYER HOLDING CROSSFIRE'S ACTION (10%, within 15 m) and seventeen Bolt Turrets around a creature 40 m north:
	// sixteen within 15 m of it and one 20 m away. THE CONTROL: a hit that sent nothing (a tick, as the player character
	// announces one) is repeated by none. A hit that sent 1,000 is repeated 15 times at 100: the creature, with no
	// defences, loses 1,500. THE MEASURE THE COORDINATING SESSION ASKED FOR: the time that one hit's fifteen repeats take,
	// written to the log.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5aPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const FVector Target(0.0f, 40.0f * WarVowsV5aMetre, 0.0f);
	ACataclysmEnemyCharacter* Struck = WarVowsV5aCreatureAt(World, Target);
	if (!TestNotNull(TEXT("set-up: a creature"), Struck))
	{
		return false;
	}
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const float Angle = Index * 2.0f * PI / 16.0f;
		if (!TestNotNull(TEXT("set-up: a turret"),
						 WarVowsV5aMachineAt(Player.Character,
											 Target + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 1000.0f,
											 TEXT("BoltTurret"))))
		{
			return false;
		}
	}
	WarVowsV5aMachineAt(Player.Character, Target + FVector(2000.0f, 0.0f, 0.0f), TEXT("BoltTurret"));
	FCataclysmPoolAction Crossfire;
	Crossfire.Event = FName(TEXT("hit_dealt"));
	Crossfire.Percent = 10.0f;
	Crossfire.EventValue = 15.0f;
	Crossfire.bCrossfire = true;
	Crossfire.TriggerKey = FName(TEXT("WarVowsV5a:crossfire"));
	Player.System->SetPoolActions({Crossfire});

	const auto Hit = [&](float Sent, double* Seconds)
	{
		const float Before = WarVowsV5aHealthOf(Struck);
		const float Was = Player.System->SetHitBeingAnnouncedSent(Sent);
		const double Began = FPlatformTime::Seconds();
		Player.System->ActOnEvent(FName(TEXT("hit_dealt")), nullptr, /*EventAmount=*/Sent, /*bLanded=*/true, Struck);
		if (Seconds)
		{
			*Seconds = FPlatformTime::Seconds() - Began;
		}
		Player.System->SetHitBeingAnnouncedSent(Was);
		return Before - WarVowsV5aHealthOf(Struck);
	};
	TestEqual(TEXT("control: a hit that sent nothing is repeated by none"), Hit(0.0f, nullptr), 0.0f, 0.01f);
	double Took = 0.0;
	TestEqual(TEXT("a hit that sent 1,000 is repeated 15 times at 100"), Hit(1000.0f, &Took), 1500.0f, 1.0f);
	AddInfo(FString::Printf(TEXT("Crossfire: one hit's 15 repeats took %.3f ms in this test world"), Took * 1000.0));
	return true;
}

// ---------------------------------------------------------------------------
// Lightning Rod
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V5A_TEST(FCataclysmWarVowsV5aLightningRodTest,
	"Cataclysm.WarVowsV5a.AHitOnThePlayerCanStrikeTheNearestDeviceWithinTenMetresInstead")
{
	using namespace CataclysmWarVowsV5aTest;
	// A PLAYER HOLDING LIGHTNING ROD (3% for each device) with four machines: a Spike Trap 5 m east, the nearest, and
	// three Bolt Turrets 20 m and more away. Its chance is 12%. A creature strikes it with real blows of 500.
	// THE FIRST CONTROL: with the roll pinned at 50, above 12, the player takes the blow. Pinned at 5, the Spike Trap takes
	// it and the player nothing, and the trap, struck, goes off at the attacker. THE SECOND CONTROL: with the Spike Trap
	// gone, no device stands within 10 m, and the player takes the blow even at a roll of nought.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5aPlayer Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	Player.Holds({{UCataclysmAbilitySystemComponent::BlowToDeviceChancePerDeviceStat, 3.0f}});
	const FVector East(WarVowsV5aMetre, 0.0f, 0.0f);
	ACataclysmMinion* Spike = WarVowsV5aMachineAt(Player.Character, East * 5.0f, TEXT("SpikeTrap"));
	for (const float Metres : {20.0f, 25.0f, 30.0f})
	{
		WarVowsV5aMachineAt(Player.Character, FVector(0.0f, Metres * WarVowsV5aMetre, 0.0f), TEXT("BoltTurret"));
	}
	ACataclysmEnemyCharacter* Attacker = WarVowsV5aCreatureAt(World, FVector(-3.0f * WarVowsV5aMetre, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("set-up: a Spike Trap"), Spike) || !TestNotNull(TEXT("set-up: an attacker"), Attacker)
		|| !TestEqual(TEXT("set-up: four devices make 12%"),
					  UCataclysmAbilitySystemComponent::BlowToDeviceChanceFor(
						  Player.Character, ACataclysmMinion::DevicesOf(Player.Character).Num()),
					  12.0f, 0.01f))
	{
		return false;
	}
	const auto Blow = [&](float Roll, float& PlayerLost, float& SpikeLost, float& AttackerLost)
	{
		FWarVowsV5aPinnedRoll Pinned(Roll);
		Player.System->SetNumericAttributeBase(UCataclysmVitalAttributeSet::GetHealthAttribute(), 10000.0f);
		const float PlayerBefore = WarVowsV5aHealthOf(Player.Character);
		const float SpikeBefore = IsValid(Spike) ? WarVowsV5aHealthOf(Spike) : 0.0f;
		const float AttackerBefore = WarVowsV5aHealthOf(Attacker);
		World->TimeSeconds += 1.0f;
		UCataclysmSkillEffects::ApplyDirectDamage(Attacker, Player.Character, 500.0f);
		PlayerLost = PlayerBefore - WarVowsV5aHealthOf(Player.Character);
		SpikeLost = IsValid(Spike) ? SpikeBefore - WarVowsV5aHealthOf(Spike) : 0.0f;
		AttackerLost = AttackerBefore - WarVowsV5aHealthOf(Attacker);
	};
	float PlayerLost = 0.0f;
	float SpikeLost = 0.0f;
	float AttackerLost = 0.0f;
	Blow(50.0f, PlayerLost, SpikeLost, AttackerLost);
	TestTrue(*FString::Printf(TEXT("control: at a roll of 50 the player takes the blow: %.2f"), PlayerLost),
			 PlayerLost > 0.0f);
	TestEqual(TEXT("control: and the Spike Trap nothing"), SpikeLost, 0.0f, 0.01f);

	Blow(5.0f, PlayerLost, SpikeLost, AttackerLost);
	TestEqual(TEXT("at a roll of 5 the player takes nothing"), PlayerLost, 0.0f, 0.01f);
	TestTrue(*FString::Printf(TEXT("the Spike Trap takes the blow: %.2f"), SpikeLost), SpikeLost > 0.0f);
	TestTrue(*FString::Printf(TEXT("and, struck, goes off at the attacker: %.2f"), AttackerLost), AttackerLost > 0.0f);

	Spike->Destroy();
	Spike = nullptr;
	Blow(0.0f, PlayerLost, SpikeLost, AttackerLost);
	TestTrue(*FString::Printf(TEXT("control: with no device within 10 m the player takes it even at nought: %.2f"),
							  PlayerLost),
			 PlayerLost > 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// The options, handed to a War player
// ---------------------------------------------------------------------------

CATACLYSM_WAR_VOWS_V5A_TEST(FCataclysmWarVowsV5aHandedOverTest,
	"Cataclysm.WarVowsV5a.AWarPlayerTakingTheOptionsIsHandedTheirStatsAndActions")
{
	// A LEVEL 100 WAR PLAYER THROUGH `SetPassiveAllocation`. Lightning Rod (First Oath 3) hands over 3 for each device,
	// Creeping Mines (Third Oath 1) 300 cm a second within 1,000 cm, and the Final Oath's Where They Fall (1) and
	// Crossfire (3) their actions. THE CONTROLS: Baited, Counterfire and Magnum Opus hand over none of it.
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
	const auto Take = [&](int32 First, int32 Third, int32 Final)
	{
		FCataclysmPassiveAllocation Allocation;
		const TPair<const TCHAR*, int32> Picks[] = {{TEXT("Saboteur_capstone_25"), First},
													{TEXT("Saboteur_capstone_100"), Third},
													{TEXT("Saboteur_capstone_200"), Final}};
		for (const TPair<const TCHAR*, int32>& Each : Picks)
		{
			Allocation.Add(FName(Each.Key), 1);
			Allocation.SetChosenOption(FName(Each.Key), Each.Value);
		}
		State->SetPassiveAllocation(Allocation, TArray<FName>());
		Equipment->RefreshAttributes(System);
	};
	const auto Count = [System](bool FCataclysmPoolAction::* Flag)
	{
		int32 Found = 0;
		for (const FCataclysmPoolAction& Action : System->GetPoolActions())
		{
			Found += Action.*Flag ? 1 : 0;
		}
		return Found;
	};

	Take(1, 3, 2);
	TestEqual(TEXT("control: Baited hands over no chance for devices"),
			  UCataclysmAbilitySystemComponent::BlowToDeviceChanceFor(Character, 1), 0.0f, 0.001f);
	TestEqual(TEXT("control: Counterfire no creep"), ACataclysmMinion::TrapCreepCmPerSecondFor(Character), 0.0f, 0.001f);
	TestEqual(TEXT("control: Magnum Opus no copy at a kill"), Count(&FCataclysmPoolAction::bCopyLastTrapAtKill), 0);

	Take(3, 1, 1);
	TestEqual(TEXT("Lightning Rod: 3 for one device"), UCataclysmAbilitySystemComponent::BlowToDeviceChanceFor(Character, 1),
			  3.0f, 0.001f);
	TestEqual(TEXT("Creeping Mines: 300 cm a second"), ACataclysmMinion::TrapCreepCmPerSecondFor(Character), 300.0f,
			  0.001f);
	TestEqual(TEXT("within 1,000 cm"), ACataclysmMinion::TrapCreepReachCmFor(Character), 1000.0f, 0.001f);
	TestEqual(TEXT("Where They Fall: one copy action"), Count(&FCataclysmPoolAction::bCopyLastTrapAtKill), 1);

	Take(3, 1, 3);
	TestEqual(TEXT("Crossfire: one repeat action"), Count(&FCataclysmPoolAction::bCrossfire), 1);
	TestEqual(TEXT("control: and no copy action"), Count(&FCataclysmPoolAction::bCopyLastTrapAtKill), 0);
	return true;
}

#undef CATACLYSM_WAR_VOWS_V5A_TEST

#endif // WITH_AUTOMATION_TESTS
