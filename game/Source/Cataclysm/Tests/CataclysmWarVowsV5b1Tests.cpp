// Copyright Stephen Dubois. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AbilitySystem/CataclysmAbilitySystemComponent.h"
#include "AbilitySystem/CataclysmClassResourceAttributeSet.h"
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
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

/**
 * THE WAR VOW OPTIONS' FIFTH SLICE, SECOND HALF, FIRST PART, V5b1, 2026-11-25: Scrap Mines, on the coordinating
 * session's rulings S1 and S2 and its acceptance of the scrap trap as an inert copy of the gone gadget, marked a trap.
 * Bolt Turrets stand in for gadgets, as `CataclysmWarVowsV4bTests.cpp` uses them.
 *
 * Helper types and functions are named after the subject, so no unity build meets two of one name.
 */
namespace CataclysmWarVowsV5b1Test
{
	constexpr float WarVowsV5b1Metre = 100.0f;

	/** A possessed player at the origin with a pool of 100 holding 100, holding Scrap Mines' two actions. */
	struct FWarVowsV5b1Player
	{
		explicit FWarVowsV5b1Player(UWorld* World)
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
				TArray<FCataclysmPoolAction> Actions;
				for (const TCHAR* Event : {TEXT("gadget_destroyed"), TEXT("gadget_expired")})
				{
					FCataclysmPoolAction Scrap;
					Scrap.Event = FName(Event);
					Scrap.Percent = 200.0f;
					Scrap.EventValue = 3.0f;
					Scrap.bScrapMine = true;
					Scrap.TriggerKey = FName(*FString::Printf(TEXT("WarVowsV5b1:%s"), Event));
					Actions.Add(Scrap);
				}
				System->SetPoolActions(MoveTemp(Actions));
			}
		}
		bool IsUsable() const { return Character && System; }

		/** The scrap traps this player commands now. */
		TArray<ACataclysmMinion*> ScrapTraps() const
		{
			TArray<ACataclysmMinion*> Found = ACataclysmMinion::DevicesOf(Character);
			Found.RemoveAll([](const ACataclysmMinion* Each) { return !Each->IsScrapTrap(); });
			return Found;
		}

		ACataclysmPlayerCharacter* Character = nullptr;
		UCataclysmAbilitySystemComponent* System = nullptr;
	};

	ACataclysmEnemyCharacter* WarVowsV5b1CreatureAt(UWorld* World, const FVector& Where)
	{
		ACataclysmEnemyCharacter* Made = World->SpawnActor<ACataclysmEnemyCharacter>(Where, FRotator::ZeroRotator);
		if (Made)
		{
			Made->SetGenericTeamId(UCataclysmTeams::IdFor(ECataclysmTeam::Monsters));
			Made->SetHealth(100000.0f);
		}
		return Made;
	}

	float WarVowsV5b1HealthOf(const AActor* Actor)
	{
		const UAbilitySystemComponent* System = UCataclysmTargeting::AbilitySystemOf(Actor);
		return System ? System->GetNumericAttribute(UCataclysmVitalAttributeSet::GetHealthAttribute()) : -1.0f;
	}

	ACataclysmMinion* WarVowsV5b1TurretAt(AActor* Player, const FVector& Where, float Lifetime = 600.0f)
	{
		return ACataclysmMinion::Spawn(Player, Where, Lifetime, /*bBurns=*/false, TEXT("BoltTurret"));
	}
}

#define CATACLYSM_WAR_VOWS_V5B1_TEST(TestClass, TestName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, TestName, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
	bool TestClass::RunTest(const FString& Parameters)

CATACLYSM_WAR_VOWS_V5B1_TEST(FCataclysmWarVowsV5b1DestroyedTest,
	"Cataclysm.WarVowsV5b1.ADestroyedGadgetLeavesAScrapTrapThatFiresItsAttackOnceAtDouble")
{
	using namespace CataclysmWarVowsV5b1Test;
	// A BOLT TURRET 20 m EAST IS DESTROYED by a blow from a creature 30 m away: it leaves a scrap trap where it stood, a
	// trap and not a gadget, at half size, reserving nothing. THE MEASURE: a second turret's ordinary blow on a creature
	// far away. THE CONTROLS: before its arming time the scrap trap fires nothing at a creature 2 m from it, and its own
	// swing outside its one shot deals nothing. Armed, it fires once at double the turret's blow and is used up. A scrap
	// trap destroyed leaves no scrap of its own (THE LAST CONTROL).
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5b1Player Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	const FVector East(WarVowsV5b1Metre, 0.0f, 0.0f);
	ACataclysmMinion* Turret = WarVowsV5b1TurretAt(Player.Character, East * 20.0f);
	ACataclysmMinion* Measurer = WarVowsV5b1TurretAt(Player.Character, FVector(0.0f, -40.0f * WarVowsV5b1Metre, 0.0f));
	ACataclysmEnemyCharacter* Breaker = WarVowsV5b1CreatureAt(World, East * 50.0f);
	ACataclysmEnemyCharacter* Measure = WarVowsV5b1CreatureAt(World, FVector(0.0f, 40.0f * WarVowsV5b1Metre, 0.0f));
	if (!TestTrue(TEXT("set-up: two turrets and two creatures"), Turret && Measurer && Breaker && Measure))
	{
		return false;
	}
	const FVector Stood = Turret->GetActorLocation();
	const float MeasureBefore = WarVowsV5b1HealthOf(Measure);
	Measurer->AttackTarget(Measure);
	const float OneBlow = MeasureBefore - WarVowsV5b1HealthOf(Measure);
	if (!TestTrue(*FString::Printf(TEXT("set-up: one turret blow takes something: %.2f"), OneBlow), OneBlow > 0.0f))
	{
		return false;
	}

	UCataclysmSkillEffects::ApplyDirectDamage(Breaker, Turret, 1.0e7f);
	TestTrue(TEXT("set-up: the turret is destroyed"), UCataclysmSkillEffects::IsDead(Turret));
	const TArray<ACataclysmMinion*> Scraps = Player.ScrapTraps();
	if (!TestEqual(TEXT("it leaves one scrap trap"), Scraps.Num(), 1))
	{
		return false;
	}
	ACataclysmMinion* Scrap = Scraps[0];
	TestTrue(TEXT("where it stood"), FVector::Dist2D(Scrap->GetActorLocation(), Stood) < 1.0f);
	TestTrue(TEXT("a trap"), Scrap->IsTrap());
	TestFalse(TEXT("and not a gadget"), Scrap->IsGadget());
	TestEqual(TEXT("at half size"), static_cast<float>(Scrap->GetActorScale3D().X), static_cast<float>(Turret->GetActorScale3D().X * 0.5f), 0.001f);
	TestEqual(TEXT("reserving nothing"), Scrap->ReservedFervour, 0.0f, 0.001f);

	ACataclysmEnemyCharacter* Comer = WarVowsV5b1CreatureAt(World, Stood + East * 2.0f);
	const float ComerBefore = WarVowsV5b1HealthOf(Comer);
	ACataclysmMinion::GadgetStep(Scrap);
	TestEqual(TEXT("control: before its arming time it fires nothing"), WarVowsV5b1HealthOf(Comer), ComerBefore, 0.01f);
	Scrap->AttackTarget(Comer);
	TestEqual(TEXT("control: and its own swing deals nothing"), WarVowsV5b1HealthOf(Comer), ComerBefore, 0.01f);

	World->TimeSeconds += 2.0f;
	ACataclysmMinion::GadgetStep(Scrap);
	TestEqual(TEXT("armed, it fires once at double the turret's blow"), ComerBefore - WarVowsV5b1HealthOf(Comer),
			  2.0f * OneBlow, 0.5f);
	TestEqual(TEXT("and is used up"), Player.ScrapTraps().Num(), 0);


	UCataclysmSkillEffects::ApplyDirectDamage(Breaker, Measurer, 1.0e7f);
	const TArray<ACataclysmMinion*> Second = Player.ScrapTraps();
	if (TestEqual(TEXT("set-up: the second turret, destroyed, leaves a scrap trap"), Second.Num(), 1))
	{
		UCataclysmSkillEffects::ApplyDirectDamage(Breaker, Second[0], 1.0e7f);
		TestEqual(TEXT("control: a scrap trap destroyed leaves no scrap of its own"), Player.ScrapTraps().Num(), 0);
	}
	return true;
}

CATACLYSM_WAR_VOWS_V5B1_TEST(FCataclysmWarVowsV5b1ExpiredTest,
	"Cataclysm.WarVowsV5b1.AGadgetWhoseLifeRunsOutRaisesGadgetExpiredAndLeavesAScrapTrap")
{
	using namespace CataclysmWarVowsV5b1Test;
	// A BOLT TURRET WITH A LIFE OF A TENTH OF A SECOND: when the timer runs it out, `gadget_expired` is raised and a scrap
	// trap stands where it stood. THE CONTROL: before the timer runs, none stands.
	UWorld* World = CataclysmTestWorld::MakeWorldThatHasBegunPlay();
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	const FWarVowsV5b1Player Player(World);
	if (!TestTrue(TEXT("set-up: a possessed player"), Player.IsUsable()))
	{
		return false;
	}
	ACataclysmMinion* Turret =
		WarVowsV5b1TurretAt(Player.Character, FVector(20.0f * WarVowsV5b1Metre, 0.0f, 0.0f), /*Lifetime=*/0.1f);
	if (!TestNotNull(TEXT("set-up: a turret"), Turret))
	{
		return false;
	}
	Turret->SetLifeSpan(0.1f);
	TestEqual(TEXT("control: before its life runs out, no scrap trap"), Player.ScrapTraps().Num(), 0);
	World->GetTimerManager().Tick(0.5f);
	TestEqual(TEXT("when it runs out, a scrap trap stands"), Player.ScrapTraps().Num(), 1);
	return true;
}

CATACLYSM_WAR_VOWS_V5B1_TEST(FCataclysmWarVowsV5b1HandedOverTest,
	"Cataclysm.WarVowsV5b1.AWarPlayerTakingScrapMinesIsHandedBothActions")
{
	// A LEVEL 100 WAR PLAYER THROUGH `SetPassiveAllocation`: Scrap Mines (Second Oath 2) hands over two scrap actions, on
	// `gadget_destroyed` and `gadget_expired`, at 200% within 3 m. THE CONTROL: Trail of Wire (option 1) hands over none.
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
	const auto Take = [&](int32 Option)
	{
		FCataclysmPassiveAllocation Allocation;
		Allocation.Add(FName(TEXT("Saboteur_capstone_50")), 1);
		Allocation.SetChosenOption(FName(TEXT("Saboteur_capstone_50")), Option);
		State->SetPassiveAllocation(Allocation, TArray<FName>());
		Equipment->RefreshAttributes(System);
	};
	const auto Scraps = [System]()
	{
		TArray<FCataclysmPoolAction> Found;
		for (const FCataclysmPoolAction& Action : System->GetPoolActions())
		{
			if (Action.bScrapMine)
			{
				Found.Add(Action);
			}
		}
		return Found;
	};
	Take(1);
	TestEqual(TEXT("control: Trail of Wire hands over no scrap action"), Scraps().Num(), 0);
	Take(2);
	const TArray<FCataclysmPoolAction> Found = Scraps();
	if (TestEqual(TEXT("Scrap Mines hands over two"), Found.Num(), 2))
	{
		TArray<FName> Events = {Found[0].Event, Found[1].Event};
		TestTrue(TEXT("one on gadget_destroyed"), Events.Contains(FName(TEXT("gadget_destroyed"))));
		TestTrue(TEXT("and one on gadget_expired"), Events.Contains(FName(TEXT("gadget_expired"))));
		TestEqual(TEXT("at 200%"), Found[0].Percent, 200.0f, 0.001f);
		TestEqual(TEXT("within 3 m"), Found[0].EventValue, 3.0f, 0.001f);
	}
	return true;
}

#undef CATACLYSM_WAR_VOWS_V5B1_TEST

#endif // WITH_AUTOMATION_TESTS
